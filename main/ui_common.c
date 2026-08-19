#include "ui_common.h"

#include "fb_capture.h"

/* ------------------------------------------------------------------ status */

// Deletes the one-shot camera-flash overlay -- see screenshot_hotspot_cb().
static void screenshot_flash_del_cb(lv_timer_t *t)
{
    lv_obj_t *flash = (lv_obj_t *)lv_timer_get_user_data(t);
    if (flash) lv_obj_delete(flash);
}

// Invisible top-left corner hotspot's tap handler -- saves whatever screen
// is currently showing to the SD card (main/fb_capture.c's
// fb_capture_save_to_sd()), added specifically for capturing screens while
// genuinely untethered outdoors (e.g. to get a real GPS fix) -- there's no
// USB connection available there to pull the normal "SNAP GET" capture
// through. A brief full-screen flash is the only feedback available (white
// for saved, red for not -- no SD card mounted, or nothing to capture) --
// there's no serial console to check a log line against out in the field
// either, this has to be self-explanatory on the screen alone.
static void screenshot_hotspot_cb(lv_event_t *e)
{
    lv_obj_t *hotspot = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *scr = lv_obj_get_screen(hotspot);

    bool ok = fb_capture_save_to_sd();

    lv_obj_t *flash = ui_box(scr);
    lv_obj_add_flag(flash, LV_OBJ_FLAG_FLOATING); // skip scr's own flex layout, same
                                                   // reasoning as ui_home.c's confirm scrim
    lv_obj_remove_flag(flash, LV_OBJ_FLAG_CLICKABLE); // don't eat the tap that dismisses it
    lv_obj_set_pos(flash, 0, 0);
    lv_obj_set_size(flash, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(flash, ok ? lv_color_white() : UI_C_RED, 0);
    lv_obj_set_style_bg_opa(flash, LV_OPA_COVER, 0);
    lv_obj_move_foreground(flash);

    lv_timer_t *timer = lv_timer_create(screenshot_flash_del_cb, 150, flash);
    lv_timer_set_repeat_count(timer, 1);
}

void ui_status_create(lv_obj_t *parent, ui_status_t *out)
{
    // Flat: root IS the row. No intermediate LV_SIZE_CONTENT flex container
    // between root and the labels -- that nesting is exactly what kept this
    // bar from ever rendering on the 66px screens (see ui_common.h's own
    // comment on this function for the full history). Vertical centering
    // comes from the cross-axis align below rather than top/bottom padding,
    // so there's no height arithmetic to get wrong either.
    lv_obj_t *root = ui_box(parent);
    lv_obj_set_size(root, LV_PCT(100), UI_STATUS_H);
    lv_obj_set_style_pad_hor(root, 22, 0);
    ui_flex_row(root, 12);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    out->root = root;

    out->dot = ui_box(root);
    lv_obj_set_size(out->dot, 20, 20);
    lv_obj_set_style_radius(out->dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(out->dot, UI_C_GREEN, 0);
    lv_obj_set_style_bg_opa(out->dot, LV_OPA_COVER, 0);

    out->fix  = ui_label(root, "GPS FIX", ui_font.semi_m, UI_C_GREEN);
    out->sats = ui_label(root, "14 sats", ui_font.s, UI_C_MUTED);

    lv_obj_t *spacer = ui_box(root);
    lv_obj_set_flex_grow(spacer, 1);

    out->clock = ui_label(root, "10:24 AM", ui_font.s, UI_C_TEXT);
    out->batt  = ui_label(root, "87%", ui_font.s, UI_C_GREEN);

    // Invisible screenshot-to-SD hotspot, see screenshot_hotspot_cb()'s own
    // comment. FLOATING so it sits at a fixed (0,0) position instead of
    // becoming another item in root's flex row; added last so it's also
    // drawn last (on top) and wins the tap over the dot/fix-text beneath
    // it, which have never had a handler of their own anyway (ui_box()'s
    // default CLICKABLE + deepest-clickable-wins hit testing already
    // routes taps there to nothing today).
    lv_obj_t *hotspot = ui_box(root);
    lv_obj_add_flag(hotspot, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(hotspot, 0, 0);
    lv_obj_set_size(hotspot, 80, UI_STATUS_H);
    lv_obj_set_style_bg_opa(hotspot, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(hotspot, screenshot_hotspot_cb, LV_EVENT_CLICKED, NULL);
}

void ui_status_set_fix(ui_status_t *s, const char *fix_text, bool good)
{
    if (!s) return;
    lv_label_set_text(s->fix, fix_text);
    lv_color_t c = good ? UI_C_GREEN : UI_C_RED;
    lv_obj_set_style_text_color(s->fix, c, 0);
    lv_obj_set_style_bg_color(s->dot, c, 0);
}

void ui_status_set_sats(ui_status_t *s, int in_solution)
{
    if (!s) return;
    if (s->sats) lv_label_set_text_fmt(s->sats, "%d sats", in_solution);
}

void ui_status_set_clock(ui_status_t *s, const char *clock_text)
{
    if (s && s->clock) lv_label_set_text(s->clock, clock_text);
}

void ui_status_set_battery(ui_status_t *s, int percent)
{
    if (!s || !s->batt) return;
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    lv_label_set_text_fmt(s->batt, "%d%%", percent);
    // Red below 15%, same threshold the old two-row variant's battery-glyph
    // fill used -- the glyph itself went away with that row, so the text
    // color carries the low-battery warning now.
    lv_obj_set_style_text_color(s->batt, percent <= 15 ? UI_C_RED : UI_C_GREEN, 0);
}

void ui_status_set_battery_external(ui_status_t *s)
{
    if (!s || !s->batt) return;
    lv_label_set_text(s->batt, LV_SYMBOL_CHARGE);
    lv_obj_set_style_text_color(s->batt, UI_C_BLUE, 0);
}

/* ------------------------------------------------------------------ navbar */

static const char *tab_text[UI_TAB_COUNT] = {
    "HOME", "MAP", "NAV", "TELEMETRY", "MORE"
};

/* Stock symbols stand in for the line icons in the mockup. Swap in your own
 * 34px icon font or lv_image sources here when you have them. */
static const char *tab_icon[UI_TAB_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_IMAGE, LV_SYMBOL_UP, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS
};

lv_obj_t *ui_navbar_create(lv_obj_t *parent, ui_tab_t active, lv_event_cb_t cb)
{
    lv_obj_t *bar = ui_box(parent);
    lv_obj_set_size(bar, LV_PCT(100), UI_NAVBAR_H);
    lv_obj_set_style_bg_color(bar, UI_C_NAVBAR, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bar, UI_C_DIVIDER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    ui_flex_row(bar, 0);

    for (int i = 0; i < UI_TAB_COUNT; i++) {
        bool on = (i == (int)active);
        lv_color_t c = on ? UI_C_BLUE : UI_C_ICON;

        lv_obj_t *cell = ui_box(bar);
        lv_obj_set_height(cell, LV_PCT(100));
        lv_obj_set_flex_grow(cell, 1);
        ui_flex_col(cell, 6);
        lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        if (cb) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(cell, cb, LV_EVENT_CLICKED,
                                (void *)(lv_uintptr_t)i);
        }

        lv_obj_t *ic = ui_label(cell, tab_icon[i], ui_font.m, c);
        lv_obj_set_style_text_align(ic, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *tx = ui_label(cell, tab_text[i], ui_font.xs, c);
        if (on) lv_obj_set_style_text_font(tx, ui_font.semi_s, 0);

        lv_obj_t *ind = ui_box(cell);
        lv_obj_set_size(ind, 56, 3);
        lv_obj_set_style_radius(ind, 2, 0);
        lv_obj_set_style_bg_color(ind, UI_C_BLUE, 0);
        lv_obj_set_style_bg_opa(ind, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    return bar;
}
