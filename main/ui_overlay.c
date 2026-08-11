// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// On-screen zoom +/- buttons: no font rendering needed, a "+"/"-" glyph is
// just one or two thin filled rects. Drawn straight into the native hw
// framebuffer -- logical space is the same orientation as the native panel
// (see map_config.h), so no placement transform is needed. Owns its own
// small PPA fill client, blocking mode -- only a handful of tiny fills per
// frame, not worth pipelining.

#include "ui_overlay.h"
#include "board_interface.h"
#include "font_terminus24.h"
#include "gps.h"
#include "map_config.h"

#include <stdio.h>
#include <string.h>

#include "driver/ppa.h"
#include "esp_log.h"

static const char *TAG = "UI_OVERLAY";
static ppa_client_handle_t s_ppa_fill;

// Buttons stack up from the top of the navbar now, not the bottom of the
// full logical screen -- MAP_NAVBAR_H rows at the very bottom belong to
// ui_overlay_draw_navbar() exclusively (see its own section below).
#define BUTTON_AREA_BOTTOM (MAP_LOGICAL_H - MAP_NAVBAR_H)
#define ZOOM_IN_X   (MAP_LOGICAL_W - MAP_BUTTON_MARGIN - MAP_BUTTON_SIZE)
#define ZOOM_IN_Y   (BUTTON_AREA_BOTTOM - MAP_BUTTON_MARGIN - 2 * MAP_BUTTON_SIZE - MAP_BUTTON_GAP)
#define ZOOM_OUT_X  ZOOM_IN_X
#define ZOOM_OUT_Y  (BUTTON_AREA_BOTTOM - MAP_BUTTON_MARGIN - MAP_BUTTON_SIZE)
#define GLYPH_PAD   16

// Home/"locate me" button -- same column, stacked directly above zoom-in.
#define HOME_X      ZOOM_IN_X
#define HOME_Y      (ZOOM_IN_Y - MAP_BUTTON_GAP - MAP_BUTTON_SIZE)

void ui_overlay_init(void)
{
    ppa_client_config_t fill_cfg = { .oper_type = PPA_OPERATION_FILL, .max_pending_trans_num = 1 };
    ESP_ERROR_CHECK(ppa_register_client(&fill_cfg, &s_ppa_fill));
    ESP_LOGI(TAG, "zoom buttons ready: in (%d,%d) out (%d,%d), %dpx",
             ZOOM_IN_X, ZOOM_IN_Y, ZOOM_OUT_X, ZOOM_OUT_Y, MAP_BUTTON_SIZE);
}

// Identity now that logical space matches the native panel's own
// orientation (see map_config.h) -- kept as a named function rather than
// inlined at each call site so callers read the same either way, and so a
// future orientation change has one place to change.
static void logical_rect_to_native(int lx, int ly, int lw, int lh,
                                    int *nx, int *ny, int *nw, int *nh)
{
    *nx = lx;
    *ny = ly;
    *nw = lw;
    *nh = lh;
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
// placement as the rest of this file (identity), applied per-pixel instead
// of per-rect since glyphs are bitmap patterns, not solid fills. Raw RGB565
// values written directly into the hw framebuffer, same convention as
// MAP_BUTTON_BG_RGB565 elsewhere in this file (confirmed via
// board_m5stack_tab5.c: no byte-swap happens on this board's raw pixel path).
// ---------------------------------------------------------------------------

#define STATUS_CHAR_W  12
#define STATUS_CHAR_H  24
// Bar height itself (MAP_STATUS_BAR_H) now lives in map_config.h -- shared
// with tile_cache.c, which clips tile compositing to exclude this strip.
#define STATUS_MARGIN_X 8
#define STATUS_BG_RGB565   MAP_THEME_BG_RGB565     // matches ui_theme.h's screen background, not a card
#define STATUS_FG_RGB565   0xFFFF                  // white
#define STATUS_FIX_RGB565  MAP_THEME_GREEN_RGB565  // used for "FIX" instead of white

// Small square at the right edge of the bar showing whether the SD log is
// actually writable right now -- not just whether the card mounted at boot.
// Added after a real session's worth of logging silently produced nothing:
// the card had mounted fine, but nobody could tell logging wasn't actually
// happening until the walk was over and the file was checked back home.
#define STATUS_SD_ICON_SIZE   16
#define STATUS_SD_OK_RGB565    MAP_THEME_GREEN_RGB565  // ordinary RGB565, fill_logical_rect() converts it correctly
#define STATUS_SD_BAD_RGB565   MAP_THEME_RED_RGB565

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

// ---------------------------------------------------------------------------
// Home / "locate me" button -- crosshair-in-a-circle icon. The ring and
// center dot are curves, so (like the status bar's text) they're rasterized
// per-pixel with a plain squared-distance test rather than forced into
// rects; the four crosshair ticks poking out past the ring *are* straight
// bars, so those stay on the PPA fill path used everywhere else in this
// file. All of it is tiny (one ~40px-radius icon) and only redrawn on
// frames that were already going to redraw for other reasons, same
// unconditional-per-dirty-frame convention as the zoom buttons.
// ---------------------------------------------------------------------------

static void draw_ring_and_dot(uint8_t *fb, int nat_w, int nat_h, int cx, int cy)
{
    int outer = MAP_HOME_RING_RADIUS;
    int inner = MAP_HOME_RING_RADIUS - MAP_HOME_RING_THICKNESS;
    int outer2 = outer * outer;
    int inner2 = inner * inner;
    int dot2 = MAP_HOME_DOT_RADIUS * MAP_HOME_DOT_RADIUS;

    for (int dy = -outer; dy <= outer; dy++) {
        for (int dx = -outer; dx <= outer; dx++) {
            int d2 = dx * dx + dy * dy;
            if ((d2 <= outer2 && d2 >= inner2) || d2 <= dot2) {
                set_logical_pixel(fb, nat_w, nat_h, cx + dx, cy + dy, MAP_HOME_GLYPH_RGB565);
            }
        }
    }
}

static void draw_ticks(uint8_t *fb, int nat_w, int nat_h, int cx, int cy)
{
    int t = MAP_HOME_TICK_THICKNESS;
    int lo = MAP_HOME_RING_RADIUS + MAP_HOME_TICK_GAP;   // distance from center to near end
    int len = MAP_HOME_TICK_LEN;

    // North / south (vertical ticks, centered on the x axis).
    fill_logical_rect(fb, nat_w, nat_h, cx - t / 2, cy - lo - len, t, len, MAP_HOME_GLYPH_RGB565);
    fill_logical_rect(fb, nat_w, nat_h, cx - t / 2, cy + lo, t, len, MAP_HOME_GLYPH_RGB565);
    // East / west (horizontal ticks, centered on the y axis).
    fill_logical_rect(fb, nat_w, nat_h, cx - lo - len, cy - t / 2, len, t, MAP_HOME_GLYPH_RGB565);
    fill_logical_rect(fb, nat_w, nat_h, cx + lo, cy - t / 2, len, t, MAP_HOME_GLYPH_RGB565);
}

void ui_overlay_draw_home_button(bool follow_active)
{
    uint8_t *fb = board_lcd_hw_framebuffer();
    if (!fb) return;
    int nat_w = board_lcd_width();
    int nat_h = board_lcd_height();

    fill_logical_rect(fb, nat_w, nat_h, HOME_X, HOME_Y, MAP_BUTTON_SIZE, MAP_BUTTON_SIZE,
                       follow_active ? MAP_HOME_BG_ACTIVE_RGB565 : MAP_HOME_BG_RGB565);

    int cx = HOME_X + MAP_BUTTON_SIZE / 2;
    int cy = HOME_Y + MAP_BUTTON_SIZE / 2;
    draw_ring_and_dot(fb, nat_w, nat_h, cx, cy);
    draw_ticks(fb, nat_w, nat_h, cx, cy);
}

bool ui_overlay_hit_test_home(int16_t x, int16_t y)
{
    return x >= HOME_X && x < HOME_X + MAP_BUTTON_SIZE &&
           y >= HOME_Y && y < HOME_Y + MAP_BUTTON_SIZE;
}

// ---------------------------------------------------------------------------
// Tab navbar -- native mirror of main/ui_common.c's real LVGL navbar (see
// ui_overlay.h's doc comment for why a native version exists at all: the Map
// screen doesn't run LVGL). This draws with LVGL's OWN Montserrat font data
// and OWN stock icon glyphs (LV_SYMBOL_*) -- same font, same icons, as the
// real navbar -- rather than a hand-drawn stand-in font/icon set. That's
// safe to do from here despite LVGL being fully stopped while the Map
// screen owns the panel: lv_font_get_glyph_dsc()/lv_font_get_glyph_bitmap()
// are pure, thread-safe lookups into the font's own compiled-in static
// data (confirmed by reading lv_font_fmt_txt.c/lv_font.c directly) -- they
// don't touch any live display/render/object-tree state the way
// lv_obj_*/lv_scr_* APIs do, which is what actually requires the LVGL port
// lock/an active render context. Unlike the rest of this file, this one
// part genuinely needs "#include lvgl.h" -- see the comment on that below.
//
// This project's own Montserrat font builds (checked directly in the
// compiled main/../managed_components/lvgl__lvgl/src/font/lv_font_montserrat_*.c
// files' own header comments) are "--no-compress --bpp 4", i.e. every
// glyph is a flat, tightly-packed stream of 4-bit alpha nibbles (no
// compression, no per-row byte padding) -- confirmed against
// lv_font_fmt_txt_dsc.c's own bpp==4 unpacking code and its opa4_table
// (0,17,34,...255, i.e. just nibble*17). Requesting the "raw" bitmap
// (glyph_dsc.req_raw_bitmap = 1) hands back a direct pointer to that same
// packed data with no decompression/scratch-buffer step needed at all.
// ---------------------------------------------------------------------------

#include "lvgl.h"

#define NAVBAR_Y        (MAP_LOGICAL_H - MAP_NAVBAR_H)
#define NAVBAR_TABS     5
#define NAVBAR_ICON_GAP 6   // px, gap between icon and label
#define NAVBAR_IND_W    56  // px, active-tab indicator bar -- matches ui_common.c's
#define NAVBAR_IND_H    4
#define NAVBAR_IND_GAP  8   // px, gap between label and indicator bar
#define NAVBAR_CELL_W   (MAP_LOGICAL_W / NAVBAR_TABS)

// Same fonts ui_theme.c falls back to for ui_font.m (icons) / ui_font.xs
// (labels) when UI_USE_CUSTOM_FONTS is off, which it is in this build --
// see that file. Referenced directly rather than via ui_font (this file
// deliberately doesn't include ui_theme.h/pull in the rest of the LVGL
// widget layer, just the font subsystem).
#define NAVBAR_ICON_FONT  (&lv_font_montserrat_28)
#define NAVBAR_LABEL_FONT (&lv_font_montserrat_20)

// Must stay in the same order as main/ui_common.h's ui_tab_t (see this
// file's ui_overlay_draw_navbar()/hit-test doc comments in ui_overlay.h).
// Exactly ui_common.c's own tab_icon[]/tab_text[] arrays.
static const char *navbar_tab_icon[NAVBAR_TABS] = {
    LV_SYMBOL_HOME, LV_SYMBOL_IMAGE, LV_SYMBOL_UP, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS
};
static const char *navbar_tab_text[NAVBAR_TABS] = {
    "HOME", "MAP", "NAV", "TELEMETRY", "MORE",
};

// Decodes one UTF-8 codepoint at *s and advances *s past it. LV_SYMBOL_*
// macros are UTF-8-encoded private-use-area codepoints (3 bytes each);
// this file's plain ASCII tab labels are also valid (trivial) UTF-8, so
// one decoder handles both call sites below. Malformed input (shouldn't
// occur -- both string sources here are fixed, known-good) just advances
// by one byte and returns the Unicode replacement character.
static uint32_t utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint8_t c = p[0];
    uint32_t cp;
    int len;
    if (c < 0x80)             { cp = c;        len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else { *s += 1; return 0xFFFD; }
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += 1; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += len;
    return cp;
}

// Total advance width (logical px) of a UTF-8 string in `font` -- used to
// center icons/labels in their navbar cell, same idea as
// STATUS_CHAR_W*strlen() did for the fixed-width Terminus font elsewhere
// in this file, just accounting for Montserrat's real (variable, kerned)
// glyph widths instead.
static int lv_string_width_logical(const lv_font_t *font, const char *text)
{
    int w = 0;
    const char *s = text;
    while (*s) {
        const char *next_s = s;
        uint32_t letter = utf8_next(&next_s);
        const char *peek = next_s;
        uint32_t next_letter = *next_s ? utf8_next(&peek) : 0;
        lv_font_glyph_dsc_t dsc;
        if (lv_font_get_glyph_dsc(font, &dsc, letter, next_letter)) {
            w += dsc.adv_w;
        }
        s = next_s;
    }
    return w;
}

// Draws a UTF-8 string using LVGL's own font-glyph data, alpha-blended
// (LVGL's Montserrat builds are 4-bit anti-aliased masks, not 1-bit) into
// whatever's already at each destination pixel -- correct as long as
// callers draw a solid background first, which ui_overlay_draw_navbar()
// below always does. (lx, ly) is the top-left of the text's line box,
// same convention lv_draw_label.c itself uses internally (matched here:
// glyph_top = ly + (line_height - base_line) - box_h - ofs_y) -- verified
// by reading that exact formula out of lv_draw_label.c rather than
// guessing at LVGL's baseline convention.
static void draw_lv_string_logical(uint8_t *fb, int nat_w, int nat_h,
                                    const lv_font_t *font, int lx, int ly,
                                    const char *text, uint16_t color)
{
    uint8_t r_fg = (uint8_t)(((color >> 11) & 0x1F) * 255 / 31);
    uint8_t g_fg = (uint8_t)(((color >> 5) & 0x3F) * 255 / 63);
    uint8_t b_fg = (uint8_t)((color & 0x1F) * 255 / 31);

    int pen_x = lx;
    const char *s = text;
    while (*s) {
        const char *next_s = s;
        uint32_t letter = utf8_next(&next_s);
        const char *peek = next_s;
        uint32_t next_letter = *next_s ? utf8_next(&peek) : 0;

        lv_font_glyph_dsc_t dsc;
        if (!lv_font_get_glyph_dsc(font, &dsc, letter, next_letter)) {
            s = next_s;
            continue;
        }

        if (dsc.box_w > 0 && dsc.box_h > 0) {
            dsc.req_raw_bitmap = 1;
            const uint8_t *bitmap = lv_font_get_glyph_bitmap(&dsc, NULL);
            if (bitmap) {
                int glyph_x0 = pen_x + dsc.ofs_x;
                int glyph_y0 = ly + (font->line_height - font->base_line) - dsc.box_h - dsc.ofs_y;
                long bit_index = 0;
                for (int row = 0; row < dsc.box_h; row++) {
                    for (int col = 0; col < dsc.box_w; col++, bit_index++) {
                        uint8_t byte = bitmap[bit_index / 2];
                        uint8_t nibble = (bit_index & 1) ? (byte & 0x0F) : (byte >> 4);
                        if (nibble == 0) continue; // fully transparent

                        int nx, ny, nw, nh;
                        logical_rect_to_native(glyph_x0 + col, glyph_y0 + row, 1, 1, &nx, &ny, &nw, &nh);
                        if (nx < 0 || nx >= nat_w || ny < 0 || ny >= nat_h) continue;
                        uint16_t *px = &((uint16_t *)fb)[ny * nat_w + nx];

                        if (nibble == 15) { *px = color; continue; }

                        uint8_t alpha = (uint8_t)(nibble * 17);
                        uint16_t bg = *px;
                        uint8_t r_bg = (uint8_t)(((bg >> 11) & 0x1F) * 255 / 31);
                        uint8_t g_bg = (uint8_t)(((bg >> 5) & 0x3F) * 255 / 63);
                        uint8_t b_bg = (uint8_t)((bg & 0x1F) * 255 / 31);
                        uint8_t r = (uint8_t)((r_fg * alpha + r_bg * (255 - alpha)) / 255);
                        uint8_t g = (uint8_t)((g_fg * alpha + g_bg * (255 - alpha)) / 255);
                        uint8_t b = (uint8_t)((b_fg * alpha + b_bg * (255 - alpha)) / 255);
                        *px = (uint16_t)(((r * 31 / 255) << 11) | ((g * 63 / 255) << 5) | (b * 31 / 255));
                    }
                }
            }
        }
        pen_x += dsc.adv_w;
        s = next_s;
    }
}

void ui_overlay_draw_navbar(int active_tab)
{
    uint8_t *fb = board_lcd_hw_framebuffer();
    if (!fb) return;
    int nat_w = board_lcd_width();
    int nat_h = board_lcd_height();

    fill_logical_rect(fb, nat_w, nat_h, 0, NAVBAR_Y, MAP_LOGICAL_W, MAP_NAVBAR_H,
                       MAP_THEME_NAVBAR_RGB565);
    // 1px top border, matching ui_common.c's LV_BORDER_SIDE_TOP divider.
    fill_logical_rect(fb, nat_w, nat_h, 0, NAVBAR_Y, MAP_LOGICAL_W, 1,
                       MAP_THEME_DIVIDER_RGB565);

    // Vertically centers the (icon + gap + label + gap + indicator) block
    // within the bar, using each font's own real line height rather than a
    // hand-picked constant.
    int icon_h = NAVBAR_ICON_FONT->line_height;
    int label_h = NAVBAR_LABEL_FONT->line_height;
    int block_h = icon_h + NAVBAR_ICON_GAP + label_h + NAVBAR_IND_GAP + NAVBAR_IND_H;
    int top = NAVBAR_Y + (MAP_NAVBAR_H - block_h) / 2;
    int label_y = top + icon_h + NAVBAR_ICON_GAP;

    for (int i = 0; i < NAVBAR_TABS; i++) {
        bool on = (i == active_tab);
        uint16_t color = on ? MAP_THEME_BLUE_RGB565 : MAP_THEME_ICON_RGB565;

        int cell_x0 = i * NAVBAR_CELL_W;
        int cell_cx = cell_x0 + NAVBAR_CELL_W / 2;

        int icon_w = lv_string_width_logical(NAVBAR_ICON_FONT, navbar_tab_icon[i]);
        draw_lv_string_logical(fb, nat_w, nat_h, NAVBAR_ICON_FONT,
                               cell_cx - icon_w / 2, top, navbar_tab_icon[i], color);

        int label_w = lv_string_width_logical(NAVBAR_LABEL_FONT, navbar_tab_text[i]);
        draw_lv_string_logical(fb, nat_w, nat_h, NAVBAR_LABEL_FONT,
                               cell_cx - label_w / 2, label_y, navbar_tab_text[i], color);

        if (on) {
            int ind_x = cell_x0 + (NAVBAR_CELL_W - NAVBAR_IND_W) / 2;
            int ind_y = label_y + label_h + NAVBAR_IND_GAP;
            fill_logical_rect(fb, nat_w, nat_h, ind_x, ind_y, NAVBAR_IND_W, NAVBAR_IND_H,
                               MAP_THEME_BLUE_RGB565);
        }
    }
}

bool ui_overlay_hit_test_navbar(int16_t x, int16_t y, int *tab_out)
{
    if (y < NAVBAR_Y) return false;
    int idx = x / NAVBAR_CELL_W;
    if (idx < 0) idx = 0;
    if (idx >= NAVBAR_TABS) idx = NAVBAR_TABS - 1;
    *tab_out = idx;
    return true;
}
