// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Fixed-pool LRU tile cache + background generator task + PPA-based
// viewport compositor. The render path never touches SD/flash or does
// per-pixel software drawing -- it only issues hardware PPA blits (or a
// PPA fill for tiles that aren't ready yet), so panning stays smooth even
// while new tiles are still being produced off-task.

#include "tile_cache.h"
#include "board_interface.h"
#include "map_config.h"
#include "tile_sd.h"
#include "tile_synth.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "TILE_CACHE";

typedef enum { SLOT_EMPTY, SLOT_GENERATING, SLOT_READY } slot_state_t;

typedef struct {
    // _Atomic (not plain slot_state_t): generator_task() (core 1) writes
    // slot->pixels and then this field to publish a finished tile;
    // tile_cache_render_viewport() (core 0) reads this field to decide
    // whether it's safe to read slot->pixels. Plain reads/writes here would
    // be a real cross-core data race -- the atomic_fetch_add on s_ready_epoch
    // a few lines after the plain write in generator_task() does NOT cover
    // this, since a preemption between "state = SLOT_READY" and that
    // fetch_add would let the render task observe the new state via its own
    // plain read with no memory barrier having executed yet, risking a read
    // of stale/partially-written pixel data. Declaring the field _Atomic and
    // keeping ordinary assignment/comparison syntax elsewhere in this file
    // (C11 defaults those to memory_order_seq_cst, a full barrier) closes
    // that window with no other code changes needed.
    _Atomic slot_state_t state;
    int32_t tx, ty, zoom;
    uint32_t last_used;
    uint16_t *pixels; // MAP_TILE_SIZE x MAP_TILE_SIZE, RGB565
} tile_slot_t;

static tile_slot_t s_slots[MAP_CACHE_SLOTS];
static uint32_t s_frame_counter = 0;

// Bumped every time a tile finishes generating, so the render loop can tell
// "nothing changed" (same pan/zoom, no newly-arrived tile) from "redraw
// needed" and skip the whole composite+flip when idle instead of
// re-blitting an unchanged screen every tick.
static _Atomic uint32_t s_ready_epoch = 0;

// Also read by generator_task() (core 1) via is_request_still_relevant() to
// decide whether a queued request is stale -- _Atomic here purely for that
// cross-core read, not because the render thread's own same-core dirty-check
// use needed it.
static _Atomic int32_t s_last_pan_x = INT32_MIN, s_last_pan_y = INT32_MIN, s_last_zoom = INT32_MIN;
static uint32_t s_last_rendered_epoch = UINT32_MAX;

typedef struct { int32_t tx, ty, zoom; } tile_req_t;
static QueueHandle_t s_req_q;

static ppa_client_handle_t s_ppa_srm;
static ppa_client_handle_t s_ppa_fill;

// Up to MAP_PENDING_PPA_OPS transactions can be in flight per frame at once:
// all tile blits/fills for the frame are submitted non-blocking back-to-back
// (so the PPA hardware queue stays busy instead of the CPU round-tripping
// after each one), then we wait once for all of them to land before flipping.
#define MAP_PENDING_PPA_OPS  32
static SemaphoreHandle_t s_ppa_done_sem;

static bool ppa_trans_done_cb(ppa_client_handle_t client, ppa_event_data_t *edata, void *user_data)
{
    (void)client; (void)edata; (void)user_data;
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_ppa_done_sem, &hp_task_woken);
    return hp_task_woken == pdTRUE;
}

static inline int32_t floor_div(int32_t a, int32_t b)
{
    int32_t q = a / b;
    int32_t r = a % b;
    return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

static tile_slot_t *find_slot(int32_t tx, int32_t ty, int32_t zoom)
{
    for (int i = 0; i < MAP_CACHE_SLOTS; i++) {
        if (s_slots[i].state != SLOT_EMPTY && s_slots[i].tx == tx && s_slots[i].ty == ty &&
            s_slots[i].zoom == zoom) {
            return &s_slots[i];
        }
    }
    return NULL;
}

// Evict the least-recently-used READY slot (or reuse an EMPTY one). Tiles
// from a zoom level that's no longer being requested simply stop getting
// their last_used bumped and age out here naturally -- no explicit
// invalidation needed when the zoom level changes.
static tile_slot_t *alloc_slot(int32_t tx, int32_t ty, int32_t zoom)
{
    tile_slot_t *victim = NULL;
    for (int i = 0; i < MAP_CACHE_SLOTS; i++) {
        if (s_slots[i].state == SLOT_EMPTY) { victim = &s_slots[i]; break; }
        if (s_slots[i].state == SLOT_READY &&
            (!victim || s_slots[i].last_used < victim->last_used)) {
            victim = &s_slots[i];
        }
    }
    if (!victim) return NULL; // every slot is currently GENERATING -- try again next frame

    // TEMP diagnostic -- flicker investigation. Evicting a READY slot means
    // giving up an already-displayable tile to make room for a new one; if
    // this fires repeatedly for the same handful of (tx,ty,zoom) identities
    // during ordinary panning (cache nowhere near full), that's thrashing.
    if (victim->state == SLOT_READY) {
        ESP_LOGI(TAG, "evict %ld,%ld z%ld (last_used=%lu) -> %ld,%ld z%ld (now=%lu)",
                 (long)victim->tx, (long)victim->ty, (long)victim->zoom, (unsigned long)victim->last_used,
                 (long)tx, (long)ty, (long)zoom, (unsigned long)s_frame_counter);
    }

    // Fill in the new identity before flipping state: the generator task only
    // reads tx/ty/zoom for a request after it dequeues the matching one, and
    // the queue send/receive pair below provides the happens-before guarantee.
    victim->tx = tx;
    victim->ty = ty;
    victim->zoom = zoom;
    victim->last_used = s_frame_counter;
    victim->state = SLOT_GENERATING;
    return victim;
}

static void request_tile(int32_t tx, int32_t ty, int32_t zoom)
{
    tile_slot_t *slot = find_slot(tx, ty, zoom);
    if (slot) {
        slot->last_used = s_frame_counter;
        return;
    }
    slot = alloc_slot(tx, ty, zoom);
    if (!slot) return;

    tile_req_t req = { .tx = tx, .ty = ty, .zoom = zoom };
    if (xQueueSend(s_req_q, &req, 0) != pdTRUE) {
        slot->state = SLOT_EMPTY; // queue full; retry next frame
    }
}

// True if (tx,ty,zoom) still falls within the viewport (or the adjacent-zoom
// prefetch ring) implied by the most recently *rendered* pan/zoom -- i.e.
// would tile_cache_render_viewport()/request_adjacent_zoom() still ask for
// this tile if run again right now. Re-derives the same bounds those two
// use, read from the atomic viewport-state globals they publish. Used by
// generator_task() to skip a queued request that's gone stale (user panned/
// zoomed away since it was enqueued) instead of spending a full SD
// read+decode on a tile nobody will see -- see tile_cache.h's
// tile_cache_render_viewport() doc comment for the starvation failure mode
// this closes.
static bool is_request_still_relevant(int32_t tx, int32_t ty, int32_t zoom)
{
    int32_t cur_zoom = s_last_zoom;
    if (cur_zoom == INT32_MIN) return true; // nothing rendered yet -- don't skip

    int32_t delta = zoom - cur_zoom;
    if (delta < -1 || delta > 1) return false; // not the current or an adjacent zoom anymore

    int32_t pan_x = s_last_pan_x;
    int32_t pan_y = s_last_pan_y;
    int32_t margin = MAP_PREFETCH_MARGIN;

    if (delta != 0) {
        // Same world-point-under-screen-center shift request_adjacent_zoom()
        // uses to turn the current pan into a hypothetical pan at zoom+delta.
        int32_t focal_x = MAP_LOGICAL_W / 2, focal_y = MAP_LOGICAL_H / 2;
        int64_t world_x = (int64_t)pan_x + focal_x;
        int64_t world_y = (int64_t)pan_y + focal_y;
        if (delta > 0) { world_x <<= delta; world_y <<= delta; }
        else           { world_x >>= -delta; world_y >>= -delta; }
        pan_x = (int32_t)(world_x - focal_x);
        pan_y = (int32_t)(world_y - focal_y);
        margin = 0; // request_adjacent_zoom() uses no margin, exact viewport only
    }

    int32_t tx0 = floor_div(pan_x, MAP_TILE_SIZE) - margin;
    int32_t ty0 = floor_div(pan_y, MAP_TILE_SIZE) - margin;
    int32_t tx1 = floor_div(pan_x + MAP_LOGICAL_W - 1, MAP_TILE_SIZE) + margin;
    int32_t ty1 = floor_div(pan_y + MAP_LOGICAL_H - 1, MAP_TILE_SIZE) + margin;
    return tx >= tx0 && tx <= tx1 && ty >= ty0 && ty <= ty1;
}

static void generator_task(void *arg)
{
    (void)arg;
    tile_req_t req;
    while (1) {
        if (xQueueReceive(s_req_q, &req, portMAX_DELAY) != pdTRUE) continue;

        tile_slot_t *slot = find_slot(req.tx, req.ty, req.zoom);
        if (!slot || slot->state != SLOT_GENERATING) continue; // stale/evicted

        if (!is_request_still_relevant(req.tx, req.ty, req.zoom)) {
            // Free the slot without doing the expensive part -- see the
            // comment on is_request_still_relevant() and tile_cache.h's
            // tile_cache_render_viewport() doc comment. No PSRAM write
            // happened, so skip the pacing delay below too; go straight to
            // the next queued item.
            slot->state = SLOT_EMPTY;
            continue;
        }

        // SD-only for now (flash embedding deliberately disabled -- see
        // main.c) while verifying the SD read/decode path in isolation.
        // Procedural synth is still the catch-all for anything outside the
        // SD-covered area.
        if (!tile_sd_read(req.tx, req.ty, req.zoom, slot->pixels)) {
            synth_tile(req.tx, req.ty, req.zoom, slot->pixels);
        }
        slot->state = SLOT_READY;
        atomic_fetch_add(&s_ready_epoch, 1);

        // Adjacent-zoom prefetch can queue 100+ requests at once (e.g. cold
        // cache on boot). Each tile is a 128KB PSRAM write; back-to-back with
        // no pacing that's enough sustained PSRAM bus traffic to starve the
        // MIPI-DSI DMA's ~140MB/s continuous read need and underrun the
        // panel (blue/cyan flashing) -- confirmed as a real, previously-
        // diagnosed failure mode on this exact board (see
        // m5stack-tab5-ssh-terminal's OTA work). A bare xQueueReceive loop
        // also never blocks while the queue has items, so without a real
        // delay here (not taskYIELD -- that only yields to equal-or-higher
        // priority tasks, and idle is the lowest there is) this can run long
        // enough to starve the idle task and trip the watchdog too.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void tile_cache_init(void)
{
    for (int i = 0; i < MAP_CACHE_SLOTS; i++) {
        s_slots[i].state     = SLOT_EMPTY;
        s_slots[i].tx         = INT32_MIN;
        s_slots[i].ty         = INT32_MIN;
        s_slots[i].zoom       = INT32_MIN;
        s_slots[i].last_used  = 0;
        s_slots[i].pixels = heap_caps_aligned_alloc(64,
            (size_t)MAP_TILE_SIZE * MAP_TILE_SIZE * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        assert(s_slots[i].pixels);
    }

    s_req_q = xQueueCreate(MAP_CACHE_SLOTS, sizeof(tile_req_t));
    assert(s_req_q);

    s_ppa_done_sem = xSemaphoreCreateCounting(MAP_PENDING_PPA_OPS, 0);
    assert(s_ppa_done_sem);

    ppa_client_config_t srm_cfg = {
        .oper_type = PPA_OPERATION_SRM, .max_pending_trans_num = MAP_PENDING_PPA_OPS,
    };
    ESP_ERROR_CHECK(ppa_register_client(&srm_cfg, &s_ppa_srm));
    ppa_client_config_t fill_cfg = {
        .oper_type = PPA_OPERATION_FILL, .max_pending_trans_num = MAP_PENDING_PPA_OPS,
    };
    ESP_ERROR_CHECK(ppa_register_client(&fill_cfg, &s_ppa_fill));

    ppa_event_callbacks_t cbs = { .on_trans_done = ppa_trans_done_cb };
    ESP_ERROR_CHECK(ppa_client_register_event_callbacks(s_ppa_srm, &cbs));
    ESP_ERROR_CHECK(ppa_client_register_event_callbacks(s_ppa_fill, &cbs));

    xTaskCreatePinnedToCore(generator_task, "tile_gen", 4096, NULL, 3, NULL, 1);

    ESP_LOGI(TAG, "tile cache ready: %d slots x %u bytes", MAP_CACHE_SLOTS,
             (unsigned)(MAP_TILE_SIZE * MAP_TILE_SIZE * sizeof(uint16_t)));
}

static bool ppa_blit_tile(const uint16_t *src, int src_x, int src_y, int w, int h,
                           uint8_t *fb, int fb_w, int fb_h, int dst_x, int dst_y)
{
    ppa_srm_oper_config_t srm = {
        .in = {
            .buffer = src,
            .pic_w = MAP_TILE_SIZE, .pic_h = MAP_TILE_SIZE,
            .block_w = (uint32_t)w, .block_h = (uint32_t)h,
            .block_offset_x = (uint32_t)src_x, .block_offset_y = (uint32_t)src_y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = fb, .buffer_size = (uint32_t)fb_w * (uint32_t)fb_h * 2,
            .pic_w = (uint32_t)fb_w, .pic_h = (uint32_t)fb_h,
            .block_offset_x = (uint32_t)dst_x, .block_offset_y = (uint32_t)dst_y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = 1.0f, .scale_y = 1.0f,
        .mode = PPA_TRANS_MODE_NON_BLOCKING,
    };
    esp_err_t err = ppa_do_scale_rotate_mirror(s_ppa_srm, &srm);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PPA blit failed: %d", err);
        return false;
    }
    return true;
}

// PPA's *fill* operation expects fill_color_val byte-packed as 0x00RRGGBB,
// not a packed 16-bit R5G6B5 value, despite PPA_FILL_COLOR_MODE_RGB565's
// name -- confirmed empirically on real hardware, see the matching helper
// and comment in ui_overlay.c's fill_logical_rect(). Doesn't affect this
// file's PPA scale/rotate/mirror blit path (ppa_blit_tile, above), which
// reads real pixel data and has always rendered tile colors correctly --
// only this fill path, used for the not-yet-loaded placeholder color.
static inline uint32_t rgb565_to_ppa_fill_val(uint16_t rgb565)
{
    uint8_t r8 = (uint8_t)(((rgb565 >> 11) & 0x1F) * 255 / 31);
    uint8_t g8 = (uint8_t)(((rgb565 >> 5) & 0x3F) * 255 / 63);
    uint8_t b8 = (uint8_t)((rgb565 & 0x1F) * 255 / 31);
    return ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8;
}

static bool ppa_fill_rect(uint8_t *fb, int fb_w, int fb_h, int x, int y, int w, int h, uint16_t color)
{
    ppa_fill_oper_config_t fill = {
        .out = {
            .buffer = fb, .buffer_size = (uint32_t)fb_w * (uint32_t)fb_h * 2,
            .pic_w = (uint32_t)fb_w, .pic_h = (uint32_t)fb_h,
            .block_offset_x = (uint32_t)x, .block_offset_y = (uint32_t)y,
            .fill_cm = PPA_FILL_COLOR_MODE_RGB565,
        },
        .fill_block_w = (uint32_t)w, .fill_block_h = (uint32_t)h,
        .fill_color_val = rgb565_to_ppa_fill_val(color),
        .mode = PPA_TRANS_MODE_NON_BLOCKING,
    };
    esp_err_t err = ppa_do_fill(s_ppa_fill, &fill);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PPA fill failed: %d", err);
        return false;
    }
    return true;
}

// Request the screen-center-anchored viewport (+margin) at zoom+delta, so a
// single-step zoom in/out usually finds its tiles already cached by the
// time the user asks for it. Uses the same shift-based world-coordinate
// transform as map_view.c's zoom_at_point(), just to compute a hypothetical
// pan for request purposes -- doesn't touch any live pan/zoom state.
static void request_adjacent_zoom(int32_t pan_x, int32_t pan_y, int32_t zoom, int32_t delta)
{
    int32_t adj_zoom = zoom + delta;
    if (adj_zoom < MAP_MIN_ZOOM || adj_zoom > MAP_MAX_ZOOM) return;

    int32_t focal_x = MAP_LOGICAL_W / 2;
    int32_t focal_y = MAP_LOGICAL_H / 2;

    int64_t world_x = (int64_t)pan_x + focal_x;
    int64_t world_y = (int64_t)pan_y + focal_y;
    if (delta > 0) {
        world_x <<= delta;
        world_y <<= delta;
    } else {
        world_x >>= -delta;
        world_y >>= -delta;
    }
    int32_t adj_pan_x = (int32_t)(world_x - focal_x);
    int32_t adj_pan_y = (int32_t)(world_y - focal_y);

    // No margin ring here (unlike the current-zoom request below) -- this is
    // background prefetch, not what's on screen right now, and every extra
    // tile is another 128KB PSRAM write competing with the DSI for bus
    // bandwidth. Exact viewport only, ~24 tiles instead of ~48.
    int32_t rtx0 = floor_div(adj_pan_x, MAP_TILE_SIZE);
    int32_t rty0 = floor_div(adj_pan_y, MAP_TILE_SIZE);
    int32_t rtx1 = floor_div(adj_pan_x + MAP_LOGICAL_W - 1, MAP_TILE_SIZE);
    int32_t rty1 = floor_div(adj_pan_y + MAP_LOGICAL_H - 1, MAP_TILE_SIZE);

    for (int32_t ty = rty0; ty <= rty1; ty++) {
        for (int32_t tx = rtx0; tx <= rtx1; tx++) {
            request_tile(tx, ty, adj_zoom);
        }
    }
}

void tile_cache_mark_dirty(void)
{
    atomic_fetch_add(&s_ready_epoch, 1);
}

bool tile_cache_render_viewport(int32_t pan_x, int32_t pan_y, int32_t zoom, bool dragging)
{
    uint32_t epoch = atomic_load(&s_ready_epoch);
    if (pan_x == s_last_pan_x && pan_y == s_last_pan_y && zoom == s_last_zoom &&
        epoch == s_last_rendered_epoch) {
        return false; // nothing moved/zoomed and no tile finished loading -- skip the redraw
    }

    s_frame_counter++;

    uint8_t *fb = board_lcd_hw_framebuffer();
    if (!fb) return false;
    int nat_w = board_lcd_width();   // physical panel, portrait: 720
    int nat_h = board_lcd_height();  // physical panel, portrait: 1280
    int fb_w = MAP_LOGICAL_W;        // logical space == native portrait space now
    int fb_h = MAP_LOGICAL_H;        // (720x1280) -- see map_config.h

    // Request generation for the visible range plus a prefetch margin ring.
    int32_t rtx0 = floor_div(pan_x, MAP_TILE_SIZE) - MAP_PREFETCH_MARGIN;
    int32_t rty0 = floor_div(pan_y, MAP_TILE_SIZE) - MAP_PREFETCH_MARGIN;
    int32_t rtx1 = floor_div(pan_x + fb_w - 1, MAP_TILE_SIZE) + MAP_PREFETCH_MARGIN;
    int32_t rty1 = floor_div(pan_y + fb_h - 1, MAP_TILE_SIZE) + MAP_PREFETCH_MARGIN;

    for (int32_t ty = rty0; ty <= rty1; ty++) {
        for (int32_t tx = rtx0; tx <= rtx1; tx++) {
            request_tile(tx, ty, zoom);
        }
    }

    // Prefetch the adjacent zoom levels, enqueued after the current zoom's
    // requests so the generator task (FIFO) always prioritizes what's
    // actually on screen right now. Suspended while actively dragging --
    // sustained panning can otherwise inject new distinct tile requests
    // faster than the generator drains them, and with three zoom levels'
    // worth of requests competing for MAP_CACHE_SLOTS, the backlog can fill
    // entirely with stale work and starve the visible viewport of slots
    // (see is_request_still_relevant() for the other half of this fix).
    // Resumes automatically the instant dragging stops (next call with
    // dragging=false), same cadence as before.
    if (!dragging) {
        request_adjacent_zoom(pan_x, pan_y, zoom, -1);
        request_adjacent_zoom(pan_x, pan_y, zoom, +1);
    }

    // Composite exactly the tiles intersecting the visible screen. All PPA ops
    // for this frame are submitted non-blocking back-to-back so the hardware
    // queue stays busy, then we wait once for all of them before returning
    // (the caller flips the buffer right after -- writes must be done by then).
    int32_t vtx0 = floor_div(pan_x, MAP_TILE_SIZE);
    int32_t vty0 = floor_div(pan_y, MAP_TILE_SIZE);
    int32_t vtx1 = floor_div(pan_x + fb_w - 1, MAP_TILE_SIZE);
    int32_t vty1 = floor_div(pan_y + fb_h - 1, MAP_TILE_SIZE);
    int pending = 0;

    // Top edge of the tile-compositing area is pushed down past the GPS
    // status bar's rows -- ui_overlay.c owns that strip exclusively now.
    // Previously tiles and the status bar both drew into the same rows
    // every frame (tiles first, overlay on top); with double-buffering and
    // no VSYNC-gated buffer reuse, the DSI scan-out could occasionally read
    // a buffer mid-composite (after tiles were written, before the overlay
    // redrew on top), flashing raw tile pixels into the status bar for one
    // frame. Never writing tiles there at all removes that window
    // regardless of timing. See MAP_STATUS_BAR_H's comment in map_config.h.
    int32_t screen_top = pan_y + MAP_STATUS_BAR_H;

    for (int32_t ty = vty0; ty <= vty1; ty++) {
        for (int32_t tx = vtx0; tx <= vtx1; tx++) {
            int32_t tile_world_x = tx * MAP_TILE_SIZE;
            int32_t tile_world_y = ty * MAP_TILE_SIZE;

            int32_t ix0 = tile_world_x > pan_x ? tile_world_x : pan_x;
            int32_t iy0 = tile_world_y > screen_top ? tile_world_y : screen_top;
            int32_t tile_x1 = tile_world_x + MAP_TILE_SIZE;
            int32_t tile_y1 = tile_world_y + MAP_TILE_SIZE;
            int32_t screen_x1 = pan_x + fb_w;
            // Bottom edge is pulled up past the tab navbar's rows, same
            // reasoning/fix as the top-of-screen status-bar clip above --
            // ui_overlay.c owns that strip exclusively now too.
            int32_t screen_y1 = pan_y + fb_h - MAP_NAVBAR_H;
            int32_t ix1 = tile_x1 < screen_x1 ? tile_x1 : screen_x1;
            int32_t iy1 = tile_y1 < screen_y1 ? tile_y1 : screen_y1;
            if (ix1 <= ix0 || iy1 <= iy0) continue;

            int src_x = (int)(ix0 - tile_world_x);
            int src_y = (int)(iy0 - tile_world_y);
            int dst_x = (int)(ix0 - pan_x);
            int dst_y = (int)(iy0 - pan_y);
            int w = (int)(ix1 - ix0);
            int h = (int)(iy1 - iy0);

            // Portrait-native app now (see MAP_LOGICAL_W/H in map_config.h) --
            // logical space *is* the native framebuffer's own orientation, so
            // placement is a plain, unrotated block copy: src rect straight
            // out of the tile buffer, dst rect at the same coordinates in the
            // native framebuffer. No coordinate transform of any kind. (This
            // used to carry a CCW remap here to support holding the device
            // sideways against a portrait panel; that placement-side rotation
            // was the actual source of a real, reproduced-on-hardware tile
            // flicker, and turned out to be unnecessary in the first place --
            // see the git log for main/map_config.h around the MAP_LOGICAL_W/H
            // change for the full investigation.)
            tile_slot_t *slot = find_slot(tx, ty, zoom);
            bool queued;
            if (slot && slot->state == SLOT_READY) {
                slot->last_used = s_frame_counter;
                queued = ppa_blit_tile(slot->pixels, src_x, src_y, w, h,
                                        fb, nat_w, nat_h, dst_x, dst_y);
            } else {
                queued = ppa_fill_rect(fb, nat_w, nat_h, dst_x, dst_y,
                                        w, h, MAP_PLACEHOLDER_RGB565);
            }
            if (queued) pending++;
        }
    }

    // Wait for every PPA op submitted this frame to complete before the
    // caller flips the buffer -- otherwise we could display a half-drawn frame.
    for (int i = 0; i < pending; i++) {
        xSemaphoreTake(s_ppa_done_sem, portMAX_DELAY);
    }

    s_last_pan_x = pan_x;
    s_last_pan_y = pan_y;
    s_last_zoom = zoom;
    s_last_rendered_epoch = epoch;
    return true;
}
