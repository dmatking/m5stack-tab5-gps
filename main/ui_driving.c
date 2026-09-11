// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "ui_driving.h"

static ui_driving_t s_driving;

ui_driving_t *ui_driving_create(void)
{
    ui_driving_t *d = &s_driving;
    lv_memzero(d, sizeof(*d));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);  // whole screen is the exit tap target
    d->screen = scr;

    // ui_font.num_xl is nominally the biggest numeral font this theme
    // defines, but with UI_USE_CUSTOM_FONTS off (ui_theme.h's default, and
    // this project's actual build) it -- like num_m/num_l -- falls back to
    // stock lv_font_montserrat_48, LVGL's largest available Montserrat
    // size (confirmed: LVGL ships nothing bigger). That's the SAME size
    // Home's own SPEED card already uses, which is exactly the thing this
    // screen exists to fix -- so the glyph is rendered through a 3x
    // transform_scale to actually be dramatically bigger.
    //
    // Deliberately NOT laid out via flex (first attempt was, and broke):
    // transforms are paint-time only in LVGL, so flex reserves layout
    // space based on the label's small UNSCALED content box, has no idea
    // the painted result is 3x bigger, and stacks the unit label right
    // where the scaled glyph visually overflows into it. Fixed by
    // positioning both labels directly instead -- lv_obj_align()'s offset
    // is independent of either object's size, so there's no layout-vs-
    // paint mismatch to fight. The scale transform pivots around the
    // label's own center by default (confirmed in LVGL's own
    // lv_obj_draw.c), so offsetting speed's CENTER upward by ~90px (half
    // its ~180px scaled height) and putting the unit comfortably below
    // that keeps them clear of each other with real hardware-verified
    // headroom, not just estimated math. If a future pass wants true
    // large numerals instead of an upscaled bitmap, ui_theme.h already
    // declares (never-built) ui_font_bold_62/_86 for exactly that.
    d->speed = ui_label(scr, "--", ui_font.num_xl, UI_C_TEXT);
    lv_obj_set_style_transform_scale(d->speed, 256 * 3, 0);
    lv_obj_align(d->speed, LV_ALIGN_CENTER, 0, -90);

    d->speed_unit = ui_label(scr, "mph", ui_font.semi_l, UI_C_MUTED);
    lv_obj_align(d->speed_unit, LV_ALIGN_CENTER, 0, 220);

    // 16-point (or 8-, per Settings' "16-point compass" switch) heading --
    // "big letters" per request, not scaled like speed above (that's the
    // one number this screen exists for; heading is a secondary glance).
    // Plain num_l (48px, this build's largest stock font -- see speed's
    // own comment) is already clearly bigger than the small unit label
    // above it, no transform needed.
    d->heading = ui_label(scr, "--", ui_font.num_l, UI_C_TEXT);
    lv_obj_align(d->heading, LV_ALIGN_CENTER, 0, 320);

    return d;
}

void ui_driving_set_speed(ui_driving_t *d, float speed, const char *unit, bool valid)
{
    if (!d) return;
    if (valid) {
        lv_label_set_text_fmt(d->speed, "%d", (int)(speed + 0.5f));
        lv_obj_set_style_text_color(d->speed, UI_C_TEXT, 0);
    } else {
        lv_label_set_text(d->speed, "--");
        lv_obj_set_style_text_color(d->speed, UI_C_MUTED, 0);
    }
    if (unit) lv_label_set_text(d->speed_unit, unit);
}

void ui_driving_set_heading(ui_driving_t *d, const char *cardinal, bool valid)
{
    if (!d) return;
    if (valid && cardinal) {
        lv_label_set_text(d->heading, cardinal);
        lv_obj_set_style_text_color(d->heading, UI_C_TEXT, 0);
    } else {
        lv_label_set_text(d->heading, "--");
        lv_obj_set_style_text_color(d->heading, UI_C_MUTED, 0);
    }
}

void ui_driving_set_exit_cb(ui_driving_t *d, lv_event_cb_t cb, void *user_data)
{
    if (d && cb) lv_obj_add_event_cb(d->screen, cb, LV_EVENT_CLICKED, user_data);
}
