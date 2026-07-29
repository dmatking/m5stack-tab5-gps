// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Pan/zoom state + the touch-gesture -> render loop for the map view.
// Handles on top of the existing tile-cache/PPA render path:
//   - single-finger drag: pan
//   - on-screen +/- buttons: one zoom step, centered on screen center
//   - double-tap: one zoom-in step centered on the tapped point
// (Two-finger pinch was tried and dropped -- every zoom step is a 100%
// cache miss across the whole screen, and pinch could fire several in quick
// succession. Single, predictable +-1 steps let tile_cache.c prefetch the
// adjacent zoom level ahead of time instead.)
// Touch is polled once per render iteration (a single I2C read, not a
// bottleneck); tile *production* is decoupled onto tile_cache's own
// generator task, so this loop never blocks on it -- only PPA blits/fills,
// which are hardware DMA operations, sit on the critical path.

#include "map_view.h"
#include "board_interface.h"
#include "gps.h"
#include "map_config.h"
#include "map_tiles_data.h"
#include "tile_cache.h"
#include "touch.h"
#include "ui_overlay.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAP_VIEW";

// Remap raw (native portrait panel) touch coordinates to logical (landscape)
// space. Exact inverse of tile_cache.c's placement transform
// (native_x=logical_y, native_y=LOGICAL_W-logical_x-w), for the same
// PIL-rotate(90)-verified 90 deg CCW rotation.
static inline void to_logical(int16_t raw_x, int16_t raw_y, int16_t *lx, int16_t *ly)
{
    *lx = (int16_t)(MAP_LOGICAL_W - 1 - raw_y);
    *ly = raw_x;
}

static inline int16_t touch_dist(int16_t ax, int16_t ay, int16_t bx, int16_t by)
{
    int dx = ax - bx, dy = ay - by;
    return (int16_t)sqrtf((float)(dx * dx + dy * dy));
}

// Keep the world point under (focal_x, focal_y) fixed while stepping zoom
// by delta (+1 or -1) levels. Shifts, not multiply/divide, since each step
// is exactly one zoom level (2x world-size change); int64_t intermediates
// avoid overflow.
static void zoom_at_point(int32_t *pan_x, int32_t *pan_y, int32_t *zoom,
                           int16_t focal_x, int16_t focal_y, int delta)
{
    int32_t new_zoom = *zoom + delta;
    if (new_zoom < MAP_MIN_ZOOM) new_zoom = MAP_MIN_ZOOM;
    if (new_zoom > MAP_MAX_ZOOM) new_zoom = MAP_MAX_ZOOM;
    if (new_zoom == *zoom) return;

    int64_t world_x = (int64_t)*pan_x + focal_x;
    int64_t world_y = (int64_t)*pan_y + focal_y;
    if (new_zoom > *zoom) {
        world_x <<= (new_zoom - *zoom);
        world_y <<= (new_zoom - *zoom);
    } else {
        world_x >>= (*zoom - new_zoom);
        world_y >>= (*zoom - new_zoom);
    }
    *pan_x = (int32_t)(world_x - focal_x);
    *pan_y = (int32_t)(world_y - focal_y);
    *zoom = new_zoom;
}

// Pick the embedded zoom level to start on: MAP_ZOOM if it's actually
// embedded, else whichever level main/map_tiles_data.h happens to list
// first (still beats starting at world tile (0,0), which would make every
// embedded grid unreachable without dragging millions of pixels).
static const embedded_zoom_t *pick_starting_level(void)
{
    for (int i = 0; i < MAP_EMBEDDED_ZOOM_COUNT; i++) {
        if (MAP_EMBEDDED_ZOOMS[i].zoom == MAP_ZOOM) return &MAP_EMBEDDED_ZOOMS[i];
    }
    return MAP_EMBEDDED_ZOOM_COUNT > 0 ? &MAP_EMBEDDED_ZOOMS[0] : NULL;
}

static void map_task(void *arg)
{
    (void)arg;

    int32_t pan_x = 0, pan_y = 0;
    int32_t zoom = MAP_ZOOM;

    const embedded_zoom_t *start = pick_starting_level();
    if (start) {
        int32_t grid_w = start->cols * MAP_TILE_SIZE;
        int32_t grid_h = start->rows * MAP_TILE_SIZE;
        pan_x = start->base_tx * MAP_TILE_SIZE + (grid_w - MAP_LOGICAL_W) / 2;
        pan_y = start->base_ty * MAP_TILE_SIZE + (grid_h - MAP_LOGICAL_H) / 2;
        zoom = start->zoom;
    }

    // Single-finger drag state.
    bool dragging = false;
    bool was_pressed = false;
    bool touch_is_button = false; // this press started on a zoom button -- suppress drag/tap
    int16_t last_x = 0, last_y = 0;

    // Tap / double-tap state.
    int64_t touch_down_us = 0;
    int16_t touch_down_x = 0, touch_down_y = 0;
    int16_t touch_max_move = 0;
    bool have_last_tap = false;
    int64_t last_tap_us = 0;
    int16_t last_tap_x = 0, last_tap_y = 0;

    const TickType_t period = pdMS_TO_TICKS(1000 / MAP_RENDER_FPS);
    TickType_t next_wake = xTaskGetTickCount();

    // Measures the actual back-to-back cost of a redraw, counting only the
    // gaps between frames that *did* redraw -- idle ticks (skipped by the
    // dirty-check in tile_cache_render_viewport) don't dilute this, unlike a
    // plain wall-clock window average would if the drag gesture has pauses.
    int64_t last_redraw_us = -1;
    int64_t redraw_interval_sum_us = 0;
    uint32_t redraw_interval_count = 0;
    uint32_t ticks_since_log = 0;

    gps_state_t last_gps_state;
    memset(&last_gps_state, 0, sizeof(last_gps_state));
    bool last_log_active = false;

    while (1) {
        ticks_since_log++;

        // The map's own dirty-check (tile_cache_render_viewport, below)
        // only knows about pan/zoom/tile-arrival -- it has no idea the
        // status bar's content changed, so it would otherwise skip
        // redrawing while the map itself is idle. Bump the same epoch
        // counter background tile loads use whenever GPS state (or the SD
        // write-health the status bar's icon reflects) moves -- a card
        // pulled mid-session should turn the icon red promptly, not just
        // whenever the map happens to redraw for some other reason.
        gps_state_t cur_gps_state = gps_get_state();
        bool cur_log_active = gps_log_active();
        if (memcmp(&cur_gps_state, &last_gps_state, sizeof(cur_gps_state)) != 0 ||
            cur_log_active != last_log_active) {
            last_gps_state = cur_gps_state;
            last_log_active = cur_log_active;
            tile_cache_mark_dirty();
        }

        touch_point_t raw_pt;
        bool pressed = touch_poll_multi(&raw_pt, 1) >= 1;

        int16_t lx = 0, ly = 0;
        if (pressed) {
            to_logical(raw_pt.x, raw_pt.y, &lx, &ly);
        }

        int64_t now = esp_timer_get_time();

        if (pressed) {
            if (!was_pressed) {
                // Fresh touch-down.
                touch_down_us = now;
                touch_down_x = lx;
                touch_down_y = ly;
                touch_max_move = 0;
                dragging = false;

                int delta;
                if (ui_overlay_hit_test_zoom(lx, ly, &delta)) {
                    zoom_at_point(&pan_x, &pan_y, &zoom, MAP_LOGICAL_W / 2, MAP_LOGICAL_H / 2, delta);
                    touch_is_button = true;
                } else {
                    touch_is_button = false;
                }
            } else if (!touch_is_button) {
                if (dragging) {
                    pan_x -= (int32_t)(lx - last_x);
                    pan_y -= (int32_t)(ly - last_y);
                }
                dragging = true;
                int16_t moved = touch_dist(lx, ly, touch_down_x, touch_down_y);
                if (moved > touch_max_move) touch_max_move = moved;
            }
            last_x = lx;
            last_y = ly;

        } else {
            if (was_pressed && !touch_is_button) {
                int64_t duration = now - touch_down_us;
                if (duration < MAP_TAP_MAX_DURATION_US && touch_max_move < MAP_TAP_MAX_MOVEMENT_PX) {
                    if (have_last_tap && (now - last_tap_us) < MAP_DOUBLE_TAP_WINDOW_US &&
                        touch_dist(touch_down_x, touch_down_y, last_tap_x, last_tap_y) < MAP_DOUBLE_TAP_RADIUS_PX) {
                        zoom_at_point(&pan_x, &pan_y, &zoom, touch_down_x, touch_down_y, +1);
                        have_last_tap = false; // consumed -- don't chain into a triple-tap
                    } else {
                        have_last_tap = true;
                        last_tap_us = now;
                        last_tap_x = touch_down_x;
                        last_tap_y = touch_down_y;
                    }
                }
            }
            dragging = false;
            touch_is_button = false;
        }

        was_pressed = pressed;

        if (tile_cache_render_viewport(pan_x, pan_y, zoom)) {
            ui_overlay_draw_zoom_buttons();
            ui_overlay_draw_gps_status();
            board_lcd_commit();

            if (last_redraw_us >= 0) {
                redraw_interval_sum_us += now - last_redraw_us;
                redraw_interval_count++;
            }
            last_redraw_us = now;

            if (redraw_interval_count >= 30) {
                double avg_ms = (redraw_interval_sum_us / (double)redraw_interval_count) / 1000.0;
                ESP_LOGI(TAG, "redraw: %.1f ms avg (%.1f fps) over last %u redraws / %u loop ticks, "
                              "pan %ld,%ld zoom %ld",
                         avg_ms, 1000.0 / avg_ms, redraw_interval_count, ticks_since_log,
                         (long)pan_x, (long)pan_y, (long)zoom);
                redraw_interval_sum_us = 0;
                redraw_interval_count = 0;
                ticks_since_log = 0;
            }
        }

        vTaskDelayUntil(&next_wake, period);
    }
}

void map_view_start(void)
{
    xTaskCreatePinnedToCore(map_task, "map_view", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "map view started");
}
