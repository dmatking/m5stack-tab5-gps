/*
 * ui_theme.h — shared palette, fonts, and card/label helpers
 * Target: LVGL 9.x, 720 x 1280 portrait (M5Stack Tab5)
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Set to 1 once you have converted the IBM Plex Sans faces (see README).
 * At 0 the UI builds against the stock Montserrat fonts so you can flash
 * something immediately; big numerals will look smaller than the mockup. */
#ifndef UI_USE_CUSTOM_FONTS
#define UI_USE_CUSTOM_FONTS 0
#endif

#if UI_USE_CUSTOM_FONTS
LV_FONT_DECLARE(ui_font_20)        /* IBM Plex Sans Regular  20px */
LV_FONT_DECLARE(ui_font_24)        /* Regular  24px */
LV_FONT_DECLARE(ui_font_28)        /* Regular  28px */
LV_FONT_DECLARE(ui_font_semi_24)   /* SemiBold 24px */
LV_FONT_DECLARE(ui_font_semi_32)   /* SemiBold 32px */
LV_FONT_DECLARE(ui_font_semi_40)   /* SemiBold 40px */
LV_FONT_DECLARE(ui_font_bold_48)   /* Bold 48px, digits + . , ° ± ' */
LV_FONT_DECLARE(ui_font_bold_62)   /* Bold 62px, digits subset */
LV_FONT_DECLARE(ui_font_bold_86)   /* Bold 86px, digits subset */
#endif

/* QA aid, not a real theme option: when on, ui_mark_placeholder() tints a
 * card's background light red -- used to flag cards across every screen
 * that are still showing decorative/demo values rather than anything
 * wired to real data. One flag to flip both ways: set to 0 to build with
 * every card back to its normal color without hunting down each call
 * site (see ui_mark_placeholder()'s call sites for the actual list). */
#ifndef UI_DEBUG_MARK_PLACEHOLDERS
#define UI_DEBUG_MARK_PLACEHOLDERS 1
#endif

/* ---- palette (matches the mockups) ------------------------------------- */
#define UI_C_BG        lv_color_hex(0x05080C)
#define UI_C_CARD      lv_color_hex(0x0D141C)
#define UI_C_CARD_ALT  lv_color_hex(0x12202F)
#define UI_C_BORDER    lv_color_hex(0x1B2530)
#define UI_C_DIVIDER   lv_color_hex(0x141C25)
#define UI_C_NAVBAR    lv_color_hex(0x080C11)
#define UI_C_TEXT      lv_color_hex(0xFFFFFF)
#define UI_C_TEXT_2    lv_color_hex(0xB9C3CD)
#define UI_C_MUTED     lv_color_hex(0x8996A6)
#define UI_C_DIM       lv_color_hex(0x4E5A68)
#define UI_C_ICON      lv_color_hex(0x6B7A8A)
#define UI_C_GREEN     lv_color_hex(0x3ED12A)
#define UI_C_GREEN_DIM lv_color_hex(0x255F1D)
#define UI_C_BLUE      lv_color_hex(0x2F80ED)
#define UI_C_BLUE_BTN  lv_color_hex(0x1C64F2)
#define UI_C_RED       lv_color_hex(0xE5646E)
#define UI_C_MAP_BG    lv_color_hex(0x0A1017)
/* UI_C_CARD blended ~35% toward UI_C_RED -- see ui_mark_placeholder(). */
#define UI_C_CARD_PLACEHOLDER lv_color_hex(0x593039)
#define UI_C_MAP_GRID  lv_color_hex(0x0E151E)
#define UI_C_MAP_ROAD  lv_color_hex(0x243040)
#define UI_C_MAP_TRACK lv_color_hex(0x1C2735)

/* ---- geometry ----------------------------------------------------------- */
#define UI_SCREEN_W    720
#define UI_SCREEN_H    1280
#define UI_PAD_SIDE     20
#define UI_GAP          11
#define UI_RADIUS       18
#define UI_STATUS_H    109
#define UI_NAVBAR_H    116

/* ---- font table (resolved in ui_theme_init) ---------------------------- */
typedef struct {
    const lv_font_t *xs;      /* ~20px labels            */
    const lv_font_t *s;       /* ~24px body              */
    const lv_font_t *m;       /* ~28px list rows         */
    const lv_font_t *semi_s;  /* ~24px semibold          */
    const lv_font_t *semi_m;  /* ~32px semibold headings */
    const lv_font_t *semi_l;  /* ~40px semibold          */
    const lv_font_t *num_m;   /* ~48px bold numerals     */
    const lv_font_t *num_l;   /* ~62px bold numerals     */
    const lv_font_t *num_xl;  /* ~86px bold numerals     */
} ui_fonts_t;

extern ui_fonts_t ui_font;

void ui_theme_init(void);

/* ---- builders ----------------------------------------------------------- */

/* Bare container: no bg, no border, no scroll, no padding. */
lv_obj_t *ui_box(lv_obj_t *parent);

/* Rounded card in the 2A style (UI_C_CARD + 1px UI_C_BORDER). */
lv_obj_t *ui_card(lv_obj_t *parent);

/* Label helper. `text` may be NULL for an empty label. */
lv_obj_t *ui_label(lv_obj_t *parent, const char *text,
                   const lv_font_t *font, lv_color_t color);

/* Small uppercase caption ("SPEED", "CURRENT POSITION"). */
lv_obj_t *ui_caption(lv_obj_t *parent, const char *text);

/* Vertical flex with a gap, used for cards and columns. */
void ui_flex_col(lv_obj_t *obj, lv_coord_t gap);
void ui_flex_row(lv_obj_t *obj, lv_coord_t gap);

/* Centred pill button. Pass bg = UI_C_BLUE_BTN for the primary action, or
 * lv_color_hex(0) with `border` set for the outline variant. */
lv_obj_t *ui_button(lv_obj_t *parent, const char *text,
                    lv_color_t bg, lv_color_t text_color,
                    bool outline, lv_color_t border, lv_coord_t h);

/* Thin horizontal rule used inside grouped list cards. */
lv_obj_t *ui_divider(lv_obj_t *parent);

/* One tick-ring compass. Returns the container; `out_value` receives the
 * centre label ("043°") and `out_sub` the sub-label ("NE") for later updates.
 * The green north index sits at 12 o'clock; call ui_compass_set_heading() to
 * spin the value text (a rotating needle needs an image — see README). */
lv_obj_t *ui_compass(lv_obj_t *parent, lv_coord_t size,
                     lv_obj_t **out_value, lv_obj_t **out_sub);
void ui_compass_set_heading(lv_obj_t *value_label, lv_obj_t *sub_label,
                            int deg, const char *cardinal);

/* Small round clock-face icon (circle outline + two hands, hands pointing
 * to 12 and 3 -- purely decorative, doesn't track real time). LVGL's stock
 * symbol font has no clock glyph, unlike ui_compass()'s LV_SYMBOL_GPS-style
 * built-ins -- this fills that gap the same hand-drawn-primitives way
 * ui_compass() itself does. `size` is the icon's square footprint. */
lv_obj_t *ui_clock_icon(lv_obj_t *parent, lv_coord_t size, lv_color_t color);

/* QA aid -- see UI_DEBUG_MARK_PLACEHOLDERS above. No-op (and card is
 * otherwise untouched) when that flag is 0. */
void ui_mark_placeholder(lv_obj_t *card);

#ifdef __cplusplus
}
#endif
#endif /* UI_THEME_H */
