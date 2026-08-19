#include "ui_settings.h"

static ui_settings_t s_set;
static lv_obj_t *s_rows[UI_SET_COUNT];
// true only for ids created via row_value() (the 3 actionable rows) --
// ui_settings_set_value() checks this to decide whether to append the
// "> " chevron. See row_display()'s own comment for why the other 4 ids
// never set this.
static bool s_actionable[UI_SET_COUNT];

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
    // 18, not the original 12 -- rows read as small tap targets on real
    // hardware (reported directly by the user), and there was plenty of
    // vertical room to spare (this screen already ends in a flex_grow
    // spacer before the footer, see ui_settings_create()'s bottom).
    lv_obj_set_style_pad_ver(r, 18, 0);
    ui_flex_row(r, 12);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(r, title, ui_font.m, UI_C_TEXT);
    return r;
}

/* Row with a value + chevron on the right, tappable -- for the rows that
 * actually do something on tap (settings_row_cb() in design_ui.c). */
static lv_obj_t *row_value(lv_obj_t *parent, const char *title, const char *value,
                           lv_color_t color, ui_setting_id_t id, lv_obj_t **out)
{
    lv_obj_t *r = row_base(parent, title);
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *v = ui_label(r, value, ui_font.s, color);
    lv_label_set_text_fmt(v, "%s " LV_SYMBOL_RIGHT, value);
    if (out) *out = v;
    if (id < UI_SET_COUNT) {
        s_rows[id] = r;
        s_actionable[id] = true;
    }
    return r;
}

/* Row with a value on the right, plain text and not clickable -- for
 * read-only live info (Time zone/Constellations/Update rate/Track log)
 * that nothing on this screen lets you change. row_value()'s chevron
 * used to appear on these too even though a tap did nothing (see
 * settings_row_cb()'s old comment in design_ui.c) -- confusing, since it
 * implies there's something to change. Not registered in s_rows[]/
 * s_actionable[], so ui_settings_set_row_cb() never attaches a handler
 * and ui_settings_set_value() never appends the chevron for these ids. */
static lv_obj_t *row_display(lv_obj_t *parent, const char *title, const char *value,
                             lv_color_t color, lv_obj_t **out)
{
    lv_obj_t *r = row_base(parent, title);
    lv_obj_t *v = ui_label(r, value, ui_font.s, color);
    if (out) *out = v;
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
    lv_memzero(s_actionable, sizeof(s_actionable));

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
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(head, "Settings", ui_font.semi_m, UI_C_TEXT);
    // A flex_grow spacer pushing clock+batt to the right edge, not a
    // second nested SIZE_CONTENT flex row (this screen's own original
    // structure, "hr") -- that doubly-nested arrangement is what every
    // other screen's status bar avoids (see ui_common.c's ui_status_create(),
    // row 1: title, grow spacer, clock, batt, all flat). The nested version
    // rendered the clock as a garbled fragment on real hardware the moment
    // it started updating every tick instead of staying frozen (confirmed
    // by the user directly, a fixed width on the label alone didn't fix
    // it) -- flattening to the same structure every other screen already
    // uses without issue did.
    lv_obj_t *head_spacer = ui_box(head);
    lv_obj_set_flex_grow(head_spacer, 1);
    // clock: gps_ui_bridge.c pushes the real local time in every tick, same
    // as every other screen's status bar -- this one just never went
    // through ui_status_create(), which is what let it get missed and
    // stay frozen at this creation-time value until the user noticed.
    // batt: still that creation-time "87%" and staying that way -- there's
    // no fuel-gauge hardware wired up anywhere in this app yet (same gap
    // noted in ui_home.c), not something specific to this screen to fix
    // in isolation.
    s->status.clock = ui_label(head, "10:24 AM", ui_font.s, UI_C_TEXT);
    s->status.batt  = ui_label(head, "87%", ui_font.s, UI_C_GREEN);

    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    ui_flex_col(body, 10);

    /* display -------------------------------------------------------------- */
    // No ui_mark_placeholder() -- both rows are real (gps_ui_bridge.c/
    // design_ui.c wire brightness and keep-screen-on to the real backlight
    // and idle timeout). Used to also have a "Night mode" row -- removed
    // rather than left decorative: there's no night-mode visual treatment
    // (dimming/tint) built anywhere in the app for it to control, so it had
    // nothing real to become without designing that from scratch first.
    lv_obj_t *g1 = group(body, "DISPLAY");

    lv_obj_t *br = row_base(g1, "Brightness");
    lv_obj_t *brr = ui_box(br);
    lv_obj_set_size(brr, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(brr, 14);
    lv_obj_set_flex_align(brr, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    s->brightness = lv_slider_create(brr);
    // 280x14, not the original 170x10 -- too short/thin to drag precisely
    // (reported directly by the user as "wonky"): at 170px across a 5-100
    // range, each 1% step is under 2px, well under finger-drag precision
    // on a touchscreen, so small movements were overshooting several
    // percent at a time. More track length means more pixels per percent;
    // the bigger knob (9px pad, was 6) gives a bigger grab target too.
    lv_obj_set_size(s->brightness, 280, 14);
    lv_slider_set_range(s->brightness, 5, 100);
    lv_slider_set_value(s->brightness, 72, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s->brightness, UI_C_BORDER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s->brightness, UI_C_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s->brightness, UI_C_GREEN, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s->brightness, 9, LV_PART_KNOB);
    s->brightness_pct = ui_label(brr, "72%", ui_font.s, UI_C_MUTED);

    ui_divider(g1);
    s->sw_screen_on = row_switch(g1, "Keep screen on", true);

    /* units ---------------------------------------------------------------- */
    // No ui_mark_placeholder() -- the last group on this screen to go real.
    // Distance/speed and Elevation are persisted 2-way cycles (see
    // design_ui.c's settings_row_cb()/app_settings.h); Coordinate format
    // and 24-hour time were already real.
    lv_obj_t *g2 = group(body, "UNITS & FORMAT");
    row_value(g2, "Distance / speed", "mi / mph", UI_C_BLUE, UI_SET_UNITS,
              &s->value[UI_SET_UNITS]);
    ui_divider(g2);
    row_value(g2, "Elevation", "feet", UI_C_MUTED, UI_SET_ELEVATION,
              &s->value[UI_SET_ELEVATION]);
    ui_divider(g2);
    row_value(g2, "Coordinate format", "DD MM.MMMM", UI_C_MUTED,
              UI_SET_COORD_FORMAT, &s->value[UI_SET_COORD_FORMAT]);
    ui_divider(g2);
    // row_display(), not row_value() -- Central-only by design (see
    // gps_ui_bridge.c's us_central_from_utc()), not a picker, so no
    // chevron implying it's tappable.
    row_display(g2, "Time zone", "CDT (UTC-5)", UI_C_MUTED,
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
    // row_display(), not row_value() -- read-only, no chevron (see that
    // helper's own comment).
    lv_obj_t *g3 = group(body, "GNSS");
    row_display(g3, "Constellations", "GPS + GLO + GAL", UI_C_MUTED,
                &s->value[UI_SET_CONSTELLATIONS]);
    ui_divider(g3);
    row_display(g3, "Update rate", "5 Hz", UI_C_MUTED,
                &s->value[UI_SET_UPDATE_RATE]);

    /* logging -------------------------------------------------------------- */
    // No ui_mark_placeholder() -- both rows are real (gps_ui_bridge.c wires
    // gps_log_active() and sd_card_get_usage() into them), the one group on
    // this screen that's fully wired.
    lv_obj_t *g4 = group(body, "LOGGING & STORAGE");
    // row_display() -- a live status readout, not a toggle (there's no
    // write path to start/stop logging from this screen).
    row_display(g4, "Track log", "Recording", UI_C_GREEN,
                &s->value[UI_SET_TRACK_LOG]);
    ui_divider(g4);
    lv_obj_t *sd = row_base(g4, "SD card");
    s->sd_usage = ui_label(sd, "-- / -- GB", ui_font.s, UI_C_MUTED);

    /* connectivity ----------------------------------------------------- */
    // No ui_mark_placeholder() -- real (main/wifi_ui_bridge.c owns the
    // actual connect/status logic, this row just shows and routes to it).
    // row_value(), not row_display() -- this one IS tappable, unlike every
    // other row_display() in this file: it opens main/ui_wifi.c's screen
    // rather than cycling a value in place (settings_row_cb()'s UI_SET_WIFI
    // case in design_ui.c).
    lv_obj_t *g5 = group(body, "CONNECTIVITY");
    row_value(g5, "Wi-Fi", "Not connected", UI_C_MUTED, UI_SET_WIFI,
              &s->value[UI_SET_WIFI]);

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
    if (s_actionable[id]) {
        lv_label_set_text_fmt(s->value[id], "%s " LV_SYMBOL_RIGHT, text);
    } else {
        lv_label_set_text(s->value[id], text);
    }
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
    // Plain text, no chevron -- Track log is a row_display() row now (see
    // ui_settings_create()), this used to bake the chevron in directly.
    lv_label_set_text(v, recording ? "Recording" : "Not recording");
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
