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
#include "map_config.h"

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
        .fill_color_val = color,
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
