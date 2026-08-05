// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// On-screen zoom +/- buttons: no font rendering needed, a "+"/"-" glyph is
// just one or two thin filled rects. Drawn straight into the native hw
// framebuffer via the same logical->native placement transform tile_cache.c
// uses for tiles (the physical panel is portrait but the map renders in a
// logical landscape space, pre-rotated 90deg CCW -- see map_config.h).
// Owns its own small PPA fill client, blocking mode -- only a handful of
// tiny fills per frame, not worth pipelining.

#include "ui_overlay.h"
#include "board_interface.h"
#include "font_terminus24.h"
#include "gps.h"
#include "map_config.h"

#include <stdio.h>

#include "driver/ppa.h"
#include "esp_log.h"

static const char *TAG = "UI_OVERLAY";
static ppa_client_handle_t s_ppa_fill;

#define ZOOM_IN_X   (MAP_LOGICAL_W - MAP_BUTTON_MARGIN - MAP_BUTTON_SIZE)
#define ZOOM_IN_Y   (MAP_LOGICAL_H - MAP_BUTTON_MARGIN - 2 * MAP_BUTTON_SIZE - MAP_BUTTON_GAP)
#define ZOOM_OUT_X  ZOOM_IN_X
#define ZOOM_OUT_Y  (MAP_LOGICAL_H - MAP_BUTTON_MARGIN - MAP_BUTTON_SIZE)
#define GLYPH_PAD   16

void ui_overlay_init(void)
{
    ppa_client_config_t fill_cfg = { .oper_type = PPA_OPERATION_FILL, .max_pending_trans_num = 1 };
    ESP_ERROR_CHECK(ppa_register_client(&fill_cfg, &s_ppa_fill));
    ESP_LOGI(TAG, "zoom buttons ready: in (%d,%d) out (%d,%d), %dpx",
             ZOOM_IN_X, ZOOM_IN_Y, ZOOM_OUT_X, ZOOM_OUT_Y, MAP_BUTTON_SIZE);
}

// Same logical(landscape)->native(portrait) placement transform tile_cache.c
// uses for tiles: a 90deg CCW rotation, verified against PIL's rotate(90)
// pixel mapping (rx=oy, ry=(W-1)-ox) applied to a rect instead of a point.
static void logical_rect_to_native(int lx, int ly, int lw, int lh,
                                    int *nx, int *ny, int *nw, int *nh)
{
    *nx = ly;
    *ny = MAP_LOGICAL_W - lx - lw;
    *nw = lh;
    *nh = lw;
}

// PPA's *fill* operation expects fill_color_val byte-packed as 0x00RRGGBB,
// not a packed 16-bit R5G6B5 value, despite PPA_FILL_COLOR_MODE_RGB565's
// name -- confirmed empirically on real hardware (0xFFFF, intended white,
// rendered as cyan; a byte-packed 0x00FFFFFF rendered as true white). This
// is specific to the fill path: PPA's scale/rotate/blit operation (used for
// the JPEG map tiles, see tile_cache.c's ppa_blit_tile) reads real pixel
// data from a buffer that's already correctly formatted, so tile colors
// have never been affected. Converting here means every caller can keep
// writing ordinary RGB565 constants, same as the direct-pixel-write path
// elsewhere in this file (set_logical_pixel()) uses -- see
// [[reference-ppa-fill-bgr-order]] for the empirical test that found this
// (that memory's "BGR" framing turned out to be an oversimplification of
// this byte-packed-RGB888 reality; corrected here).
static inline uint32_t rgb565_to_ppa_fill_val(uint16_t rgb565)
{
    uint8_t r8 = (uint8_t)(((rgb565 >> 11) & 0x1F) * 255 / 31);
    uint8_t g8 = (uint8_t)(((rgb565 >> 5) & 0x3F) * 255 / 63);
    uint8_t b8 = (uint8_t)((rgb565 & 0x1F) * 255 / 31);
    return ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | b8;
}

static void fill_logical_rect(uint8_t *fb, int nat_w, int nat_h, int lx, int ly, int lw, int lh, uint16_t color)
{
    int nx, ny, nw, nh;
    logical_rect_to_native(lx, ly, lw, lh, &nx, &ny, &nw, &nh);

    ppa_fill_oper_config_t fill = {
        .out = {
            .buffer = fb, .buffer_size = (uint32_t)nat_w * (uint32_t)nat_h * 2,
            .pic_w = (uint32_t)nat_w, .pic_h = (uint32_t)nat_h,
            .block_offset_x = (uint32_t)nx, .block_offset_y = (uint32_t)ny,
            .fill_cm = PPA_FILL_COLOR_MODE_RGB565,
        },
        .fill_block_w = (uint32_t)nw, .fill_block_h = (uint32_t)nh,
        .fill_color_val = rgb565_to_ppa_fill_val(color),
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    esp_err_t err = ppa_do_fill(s_ppa_fill, &fill);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "button fill failed: %d", err);
    }
}

static void draw_button(uint8_t *fb, int nat_w, int nat_h, int bx, int by, bool plus)
{
    fill_logical_rect(fb, nat_w, nat_h, bx, by, MAP_BUTTON_SIZE, MAP_BUTTON_SIZE, MAP_BUTTON_BG_RGB565);

    int bar_len = MAP_BUTTON_SIZE - 2 * GLYPH_PAD;
    int t = MAP_BUTTON_GLYPH_THICKNESS;

    // Horizontal bar (both "+" and "-").
    fill_logical_rect(fb, nat_w, nat_h,
                       bx + GLYPH_PAD, by + MAP_BUTTON_SIZE / 2 - t / 2,
                       bar_len, t, MAP_BUTTON_GLYPH_RGB565);
    if (plus) {
        fill_logical_rect(fb, nat_w, nat_h,
                           bx + MAP_BUTTON_SIZE / 2 - t / 2, by + GLYPH_PAD,
                           t, bar_len, MAP_BUTTON_GLYPH_RGB565);
    }
}

void ui_overlay_draw_zoom_buttons(void)
{
    uint8_t *fb = board_lcd_hw_framebuffer();
    if (!fb) return;
    int nat_w = board_lcd_width();
    int nat_h = board_lcd_height();

    draw_button(fb, nat_w, nat_h, ZOOM_IN_X, ZOOM_IN_Y, true);
    draw_button(fb, nat_w, nat_h, ZOOM_OUT_X, ZOOM_OUT_Y, false);
}

bool ui_overlay_hit_test_zoom(int16_t x, int16_t y, int *delta)
{
    if (x >= ZOOM_IN_X && x < ZOOM_IN_X + MAP_BUTTON_SIZE &&
        y >= ZOOM_IN_Y && y < ZOOM_IN_Y + MAP_BUTTON_SIZE) {
        *delta = 1;
        return true;
    }
    if (x >= ZOOM_OUT_X && x < ZOOM_OUT_X + MAP_BUTTON_SIZE &&
        y >= ZOOM_OUT_Y && y < ZOOM_OUT_Y + MAP_BUTTON_SIZE) {
        *delta = -1;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// GPS status bar -- reuses the Terminus 24 font (main/font_terminus24.h,
// copied from m5stack-tab5-ssh-terminal) and the same logical->native
// rotation transform as the rest of this file, applied per-pixel instead of
// per-rect since glyphs are bitmap patterns, not solid fills. Raw RGB565
// values written directly into the hw framebuffer, same convention as
// MAP_BUTTON_BG_RGB565 elsewhere in this file (confirmed via
// board_m5stack_tab5.c: no byte-swap happens on this board's raw pixel path).
// ---------------------------------------------------------------------------

#define STATUS_CHAR_W  12
#define STATUS_CHAR_H  24
// Bar height itself (MAP_STATUS_BAR_H) now lives in map_config.h -- shared
// with tile_cache.c, which clips tile compositing to exclude this strip.
#define STATUS_MARGIN_X 8
#define STATUS_BG_RGB565   0x2104  // near-black, matches MAP_BUTTON_BG_RGB565
#define STATUS_FG_RGB565   0xFFFF  // white
#define STATUS_FIX_RGB565  0x07E0  // green, used for "FIX" instead of white

// Small square at the right edge of the bar showing whether the SD log is
// actually writable right now -- not just whether the card mounted at boot.
// Added after a real session's worth of logging silently produced nothing:
// the card had mounted fine, but nobody could tell logging wasn't actually
// happening until the walk was over and the file was checked back home.
#define STATUS_SD_ICON_SIZE   16
#define STATUS_SD_OK_RGB565    0x07E0  // green -- ordinary RGB565, fill_logical_rect() converts it correctly
#define STATUS_SD_BAD_RGB565   0xF800  // red

static inline void set_logical_pixel(uint8_t *fb, int nat_w, int nat_h, int lx, int ly, uint16_t color)
{
    int nx, ny, nw, nh;
    logical_rect_to_native(lx, ly, 1, 1, &nx, &ny, &nw, &nh);
    if (nx < 0 || nx >= nat_w || ny < 0 || ny >= nat_h) return;
    ((uint16_t *)fb)[ny * nat_w + nx] = color;
}

static void draw_char_logical(uint8_t *fb, int nat_w, int nat_h, int lx, int ly, char c, uint16_t fg)
{
    if (c < 32 || c > 127) c = ' ';
    const uint8_t (*glyph)[2] = terminus_24_data[c - 32];

    for (int gy = 0; gy < STATUS_CHAR_H; gy++) {
        uint16_t bits = ((uint16_t)glyph[gy][0] << 8) | glyph[gy][1];
        for (int gx = 0; gx < STATUS_CHAR_W; gx++) {
            if (bits & (0x8000 >> gx)) {
                set_logical_pixel(fb, nat_w, nat_h, lx + gx, ly + gy, fg);
            }
        }
    }
}

static void draw_string_logical(uint8_t *fb, int nat_w, int nat_h, int lx, int ly, const char *s, uint16_t fg)
{
    for (; *s; s++) {
        draw_char_logical(fb, nat_w, nat_h, lx, ly, *s, fg);
        lx += STATUS_CHAR_W;
    }
}

void ui_overlay_draw_gps_status(int32_t zoom)
{
    uint8_t *fb = board_lcd_hw_framebuffer();
    if (!fb) return;
    int nat_w = board_lcd_width();
    int nat_h = board_lcd_height();

    // Always repaint the full-width background first -- the string's
    // content (and thus visible length) changes as fields fill in, so a
    // stale glyph from a previous, longer string could otherwise linger.
    fill_logical_rect(fb, nat_w, nat_h, 0, 0, MAP_LOGICAL_W, MAP_STATUS_BAR_H, STATUS_BG_RGB565);

    bool fix = gps_has_fix();
    gps_state_t st = gps_get_state();

    char line[80];
    if (fix && st.latlon_valid) {
        snprintf(line, sizeof(line), "Z%-2ld  FIX  SATS:%2d  LAT:%+10.6f  LON:%+11.6f",
                 (long)zoom, st.sats_in_use, st.latitude_deg, st.longitude_deg);
    } else {
        snprintf(line, sizeof(line), "Z%-2ld  NO FIX  SATS:%2d", (long)zoom, st.sats_in_use);
    }

    int ty = (MAP_STATUS_BAR_H - STATUS_CHAR_H) / 2;
    draw_string_logical(fb, nat_w, nat_h, STATUS_MARGIN_X, ty, line, fix ? STATUS_FIX_RGB565 : STATUS_FG_RGB565);

    // Fixed position at the right edge, independent of the text's variable
    // length -- always visible regardless of fix state.
    int icon_x = MAP_LOGICAL_W - STATUS_MARGIN_X - STATUS_SD_ICON_SIZE;
    int icon_y = (MAP_STATUS_BAR_H - STATUS_SD_ICON_SIZE) / 2;
    fill_logical_rect(fb, nat_w, nat_h, icon_x, icon_y, STATUS_SD_ICON_SIZE, STATUS_SD_ICON_SIZE,
                       gps_log_active() ? STATUS_SD_OK_RGB565 : STATUS_SD_BAD_RGB565);
}
