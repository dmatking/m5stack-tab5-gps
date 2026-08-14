#include "ui_settings.h"

static ui_settings_t s_set;
static lv_obj_t *s_rows[UI_SET_COUNT];

/* Grouped list card: rounded container, rows separated by hairlines. */
static lv_obj_t *group(lv_obj_t *parent, const char *caption)
{
    ui_caption(parent, caption);
    lv_obj_t *c = ui_card(parent);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(c, 0, 0);
    ui_flex_col(c, 0);
    return c;
}

static lv_obj_t *row_base(lv_obj_t *parent, const char *title)
{
    lv_obj_t *r = ui_box(parent);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(r, 20, 0);
    lv_obj_set_style_pad_ver(r, 12, 0);
    ui_flex_row(r, 12);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(r, title, ui_font.m, UI_C_TEXT);
    return r;
}

/* Row with a value + chevron on the right. */
static lv_obj_t *row_value(lv_obj_t *parent, const char *title, const char *value,
                           lv_color_t color, ui_setting_id_t id, lv_obj_t **out)
{
    lv_obj_t *r = row_base(parent, title);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *v = ui_label(r, value, ui_font.s, color);
    lv_label_set_text_fmt(v, "%s " LV_SYMBOL_RIGHT, value);
    if (out) *out = v;
    if (id < UI_SET_COUNT) s_rows[id] = r;
    return r;
}

static lv_obj_t *row_switch(lv_obj_t *parent, const char *title, bool on)
{
    lv_obj_t *r = row_base(parent, title);
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, 74, 40);
    lv_obj_set_style_bg_color(sw, UI_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, UI_C_BLUE_BTN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, UI_C_TEXT, LV_PART_KNOB);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
}

ui_settings_t *ui_settings_create(lv_event_cb_t tab_cb)
{
    ui_settings_t *s = &s_set;
    lv_memzero(s, sizeof(*s));
    lv_memzero(s_rows, sizeof(s_rows));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    s->screen = scr;

    /* header: title instead of the GPS fix chip */
    lv_obj_t *head = ui_box(scr);
    lv_obj_set_size(head, LV_PCT(100), 66);
    lv_obj_set_style_pad_hor(head, 22, 0);
    ui_flex_row(head, 12);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(head, "Settings", ui_font.semi_m, UI_C_TEXT);
    lv_obj_t *hr = ui_box(head);
    lv_obj_set_size(hr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(hr, 16);
    lv_obj_set_flex_align(hr, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s->status.clock = ui_label(hr, "10:24 AM", ui_font.s, UI_C_TEXT);
    s->status.batt  = ui_label(hr, "87%", ui_font.s, UI_C_GREEN);

    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    ui_flex_col(body, 10);

    /* display -------------------------------------------------------------- */
    lv_obj_t *g1 = group(body, "DISPLAY");
    // Every row in every group on this screen is decorative except the
    // "24-hour time" switch in g2 below -- see project notes. Groups (not
    // individual rows) are the actual ui_card()s here, so g2's one real
    // row gets caught up in its group's tint too; noted rather than
    // engineered around, this is a QA aid, not a precision instrument.
    ui_mark_placeholder(g1);

    lv_obj_t *br = row_base(g1, "Brightness");
    lv_obj_t *brr = ui_box(br);
    lv_obj_set_size(brr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(brr, 14);
    lv_obj_set_flex_align(brr, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s->brightness = lv_slider_create(brr);
    lv_obj_set_size(s->brightness, 170, 10);
    lv_slider_set_range(s->brightness, 5, 100);
    lv_slider_set_value(s->brightness, 72, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s->brightness, UI_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s->brightness, UI_C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s->brightness, UI_C_GREEN, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s->brightness, 6, LV_PART_KNOB);
    s->brightness_pct = ui_label(brr, "72%", ui_font.s, UI_C_MUTED);

    ui_divider(g1);
    row_value(g1, "Night mode", "Auto", UI_C_BLUE, UI_SET_NIGHT_MODE,
              &s->value[UI_SET_NIGHT_MODE]);
    ui_divider(g1);
    s->sw_screen_on = row_switch(g1, "Keep screen on", true);

    /* units ---------------------------------------------------------------- */
    lv_obj_t *g2 = group(body, "UNITS & FORMAT");
    ui_mark_placeholder(g2); // see g1's comment above -- catches the real 24h-time row too
    row_value(g2, "Distance / speed", "mi / mph", UI_C_BLUE, UI_SET_UNITS,
              &s->value[UI_SET_UNITS]);
    ui_divider(g2);
    row_value(g2, "Elevation", "feet", UI_C_MUTED, UI_SET_ELEVATION,
              &s->value[UI_SET_ELEVATION]);
    ui_divider(g2);
    row_value(g2, "Coordinate format", "DD MM.MMMM", UI_C_MUTED,
              UI_SET_COORD_FORMAT, &s->value[UI_SET_COORD_FORMAT]);
    ui_divider(g2);
    row_value(g2, "Time zone", "CDT (UTC-5)", UI_C_MUTED, UI_SET_TIMEZONE,
              &s->value[UI_SET_TIMEZONE]);
    ui_divider(g2);
    // Real (persisted, see app_settings.h), unlike the rest of this group --
    // added on its own ahead of wiring the others, per the user's own
    // request. Off (12-hour) here is just the pre-sync creation-time value;
    // design_ui.c's ui_init() calls ui_settings_set_time_24h() right after
    // creation with the real persisted value.
    s->sw_time_24h = row_switch(g2, "24-hour time", false);

    /* gnss ----------------------------------------------------------------- */
    // No ui_mark_placeholder() -- both remaining rows are real (read-only)
    // displays, gps_ui_bridge.c wires them from measured/parsed GPS data.
    // Used to also have an "SBAS / WAAS" toggle -- removed rather than left
    // decorative: it had zero real backing (no way to read real SBAS status
    // from this module's NMEA output, and no write access to the module to
    // change it -- see gps.c's file header on the unused TX line), unlike
    // Constellations/Update rate which are at least real information even
    // without write access.
    lv_obj_t *g3 = group(body, "GNSS");
    row_value(g3, "Constellations", "GPS + GLO + GAL", UI_C_MUTED,
              UI_SET_CONSTELLATIONS, &s->value[UI_SET_CONSTELLATIONS]);
    ui_divider(g3);
    row_value(g3, "Update rate", "5 Hz", UI_C_MUTED, UI_SET_UPDATE_RATE,
              &s->value[UI_SET_UPDATE_RATE]);

    /* logging -------------------------------------------------------------- */
    // No ui_mark_placeholder() -- both rows are real (gps_ui_bridge.c wires
    // gps_log_active() and sd_card_get_usage() into them), the one group on
    // this screen that's fully wired.
    lv_obj_t *g4 = group(body, "LOGGING & STORAGE");
    row_value(g4, "Track log", "Recording", UI_C_GREEN,
              UI_SET_TRACK_LOG, &s->value[UI_SET_TRACK_LOG]);
    ui_divider(g4);
    lv_obj_t *sd = row_base(g4, "SD card");
    s->sd_usage = ui_label(sd, "-- / -- GB", ui_font.s, UI_C_MUTED);

    lv_obj_t *spacer = ui_box(body);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    // "AT6668", not the original design's "u-blox M10" -- gps.c's own file
    // header names the real chipset (M5Stack GPS Module v2.1); the demo
    // text named the wrong vendor entirely. Real uptime replaces the fixed
    // "21:44" on gps_ui_bridge.c's first tick.
    s->footer = ui_label(body,
        "Tab5 | FW 1.4.2 | SN 0A31-7742\nAT6668 | uptime 0:00:00",
        ui_font.xs, UI_C_DIM);

    ui_navbar_create(scr, UI_TAB_MORE, tab_cb);
    return s;
}

/* ------------------------------------------------------------------ setters */

void ui_settings_set_brightness(ui_settings_t *s, int percent)
{
    if (!s) return;
    if (percent < 5)   percent = 5;
    if (percent > 100) percent = 100;
    lv_slider_set_value(s->brightness, percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s->brightness_pct, "%d%%", percent);
}

void ui_settings_set_value(ui_settings_t *s, ui_setting_id_t id, const char *text)
{
    if (!s || id >= UI_SET_COUNT || !s->value[id] || !text) return;
    lv_label_set_text_fmt(s->value[id], "%s " LV_SYMBOL_RIGHT, text);
}

void ui_settings_set_screen_on(ui_settings_t *s, bool on)
{
    if (!s) return;
    if (on) lv_obj_add_state(s->sw_screen_on, LV_STATE_CHECKED);
    else    lv_obj_remove_state(s->sw_screen_on, LV_STATE_CHECKED);
}

void ui_settings_set_time_24h(ui_settings_t *s, bool on)
{
    if (!s) return;
    if (on) lv_obj_add_state(s->sw_time_24h, LV_STATE_CHECKED);
    else    lv_obj_remove_state(s->sw_time_24h, LV_STATE_CHECKED);
}

void ui_settings_set_storage(ui_settings_t *s, float used_gb, float total_gb)
{
    if (s) lv_label_set_text_fmt(s->sd_usage, "%.1f / %.0f GB", used_gb, total_gb);
}

void ui_settings_set_track_log(ui_settings_t *s, bool recording)
{
    if (!s) return;
    lv_obj_t *v = s->value[UI_SET_TRACK_LOG];
    lv_label_set_text_fmt(v, "%s " LV_SYMBOL_RIGHT, recording ? "Recording" : "Not recording");
    lv_obj_set_style_text_color(v, recording ? UI_C_GREEN : UI_C_MUTED, 0);
}

void ui_settings_set_footer(ui_settings_t *s, const char *line1, const char *line2)
{
    if (!s) return;
    lv_label_set_text_fmt(s->footer, "%s\n%s", line1 ? line1 : "",
                          line2 ? line2 : "");
}

void ui_settings_set_row_cb(ui_settings_t *s, lv_event_cb_t cb)
{
    LV_UNUSED(s);
    if (!cb) return;
    for (int i = 0; i < UI_SET_COUNT; i++) {
        if (s_rows[i])
            lv_obj_add_event_cb(s_rows[i], cb, LV_EVENT_CLICKED,
                                (void *)(lv_uintptr_t)i);
    }
}

void ui_settings_set_brightness_cb(ui_settings_t *s, lv_event_cb_t cb)
{
    if (s && cb)
        lv_obj_add_event_cb(s->brightness, cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void ui_settings_set_time_24h_cb(ui_settings_t *s, lv_event_cb_t cb)
{
    if (s && cb)
        lv_obj_add_event_cb(s->sw_time_24h, cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void ui_settings_set_screen_on_cb(ui_settings_t *s, lv_event_cb_t cb)
{
    if (s && cb)
        lv_obj_add_event_cb(s->sw_screen_on, cb, LV_EVENT_VALUE_CHANGED, NULL);
}
