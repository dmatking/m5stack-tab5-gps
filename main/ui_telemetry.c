#include "ui_telemetry.h"

static ui_telemetry_t s_tel;

static lv_obj_t *cell(lv_obj_t *parent, int grow, const char *caption,
                      const char *value, const lv_font_t *font,
                      lv_color_t color, lv_obj_t **out_value)
{
    lv_obj_t *c = ui_card(parent);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(c, grow);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    ui_flex_col(c, 2);
    ui_caption(c, caption);
    *out_value = ui_label(c, value, font, color);
    return c;
}

static lv_obj_t *row(lv_obj_t *parent)
{
    lv_obj_t *r = ui_box(parent);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(r, 12);
    return r;
}

ui_telemetry_t *ui_telemetry_create(lv_event_cb_t tab_cb)
{
    ui_telemetry_t *t = &s_tel;
    lv_memzero(t, sizeof(*t));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    t->screen = scr;

    ui_status_create(scr, &t->status, true);

    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    ui_flex_col(body, 12);

    lv_obj_t *r1 = row(body);
    cell(r1, 1, "SPEED",        "24.3", ui_font.num_l, UI_C_TEXT,  &t->speed);
    cell(r1, 1, "TRUE HEADING", "067\xC2\xB0", ui_font.num_l, UI_C_GREEN, &t->heading);

    /* position ------------------------------------------------------------- */
    lv_obj_t *pos = ui_card(body);
    lv_obj_set_width(pos, LV_PCT(100));
    lv_obj_set_height(pos, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pos, 20, 0);
    lv_obj_set_style_pad_ver(pos, 16, 0);
    ui_flex_col(pos, 6);
    // "|" not "\xC2\xB7" (·) throughout this file -- see ui_home.c's ±
    // comment; same missing-glyph issue (LVGL's built-in Montserrat fonts
    // only include ASCII + the degree sign), different character, same
    // ASCII-substitute fix.
    ui_caption(pos, "POSITION | WGS84");
    t->pos_line1 = ui_label(pos, "32\xC2\xB0 54.1234' N", ui_font.semi_l, UI_C_TEXT);
    t->pos_line2 = ui_label(pos, "097\xC2\xB0 19.5678' W", ui_font.semi_l, UI_C_TEXT);
    t->pos_dd    = ui_label(pos, "32.902057, -97.326130 | DD", ui_font.s, UI_C_MUTED);

    lv_obj_t *r2 = row(body);
    cell(r2, 1, "ALTITUDE MSL",   "1,248 ft", ui_font.num_m, UI_C_TEXT, &t->altitude);
    cell(r2, 1, "VERTICAL SPEED", "+210 fpm", ui_font.num_m, UI_C_TEXT, &t->vspeed);

    lv_obj_t *r3 = row(body);
    cell(r3, 1, "SATELLITES", "14/19", ui_font.num_m, UI_C_TEXT,  &t->sats);
    cell(r3, 1, "HDOP",       "0.8",   ui_font.num_m, UI_C_GREEN, &t->hdop);
    // "+/-" not ± -- see the ui_common.c-wide comment above; LVGL's built-in
    // fonts don't have that glyph, so it rendered as a tofu box on real
    // hardware. (This used to be split hex-escape literals, "\xB1" "9.4",
    // to dodge a *different*, compile-time-only bug where C's greedy \x
    // escape consumed the following digits too -- moot now that there's no
    // \x escape here at all.)
    cell(r3, 1, "ACCURACY",   "+/-9.4", ui_font.num_m, UI_C_TEXT, &t->accuracy);

    lv_obj_t *r4 = row(body);
    // Not using cell() here (unlike every other card on this screen) --
    // its caption is static, and this one needs to flip between "LOCAL |
    // CDT" and "LOCAL | CST" as daylight time comes and goes (see
    // ui_telemetry_set_time()), so the caption label itself needs to
    // survive past creation the same way t->local_time already does.
    lv_obj_t *local_card = ui_card(r4);
    lv_obj_set_height(local_card, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(local_card, 1);
    lv_obj_set_style_pad_hor(local_card, 20, 0);
    lv_obj_set_style_pad_ver(local_card, 14, 0);
    ui_flex_col(local_card, 2);
    t->local_caption = ui_caption(local_card, "LOCAL | CDT");
    t->local_time = ui_label(local_card, "10:24:18", ui_font.num_m, UI_C_TEXT);
    cell(r4, 1, "UTC", "15:24:18", ui_font.num_m, UI_C_MUTED, &t->utc_time);

    lv_obj_t *r5 = row(body);
    cell(r5, 1, "TRIP",      "12.48", ui_font.num_m, UI_C_TEXT, &t->trip);
    cell(r5, 1, "MAX SPEED", "68.3",  ui_font.num_m, UI_C_TEXT, &t->max_speed);
    cell(r5, 1, "MOVING",    "0:28",  ui_font.num_m, UI_C_TEXT, &t->moving);

    /* signal --------------------------------------------------------------- */
    lv_obj_t *sig = ui_card(body);
    // gps.c doesn't parse GSV yet, so these bars/constellations are demo
    // values -- see project notes. Everything else on this screen is real.
    ui_mark_placeholder(sig);
    lv_obj_set_width(sig, LV_PCT(100));
    lv_obj_set_flex_grow(sig, 1);
    lv_obj_set_style_pad_hor(sig, 20, 0);
    lv_obj_set_style_pad_ver(sig, 16, 0);
    ui_flex_col(sig, 10);
    lv_obj_set_flex_align(sig, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *shead = ui_box(sig);
    lv_obj_set_size(shead, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(shead, 8);
    lv_obj_set_flex_align(shead, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // "-" not "\xE2\x80\x94" (em dash) -- same missing-glyph issue as the ·
    // fix above, just a different character (also outside LVGL's built-in
    // font's ASCII+degree-sign-only coverage).
    t->signal_caption  = ui_caption(shead, "SIGNAL - 14 IN SOLUTION");
    t->constellations  = ui_label(shead, "GPS | GLONASS | GALILEO",
                                  ui_font.xs, UI_C_GREEN);

    lv_obj_t *bars = ui_box(sig);
    lv_obj_set_size(bars, LV_PCT(100), 96);
    ui_flex_row(bars, 8);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    const int demo[UI_TELEM_BARS] = { 72, 94, 60, 84, 44, 68, 36, 56, 26, 78, 32, 64 };
    for (int i = 0; i < UI_TELEM_BARS; i++) {
        t->bars[i] = ui_box(bars);
        lv_obj_set_flex_grow(t->bars[i], 1);
        lv_obj_set_height(t->bars[i], LV_PCT(demo[i]));
        lv_obj_set_style_radius(t->bars[i], 3, 0);
        lv_obj_set_style_bg_color(t->bars[i],
                                  demo[i] >= 40 ? UI_C_GREEN : UI_C_GREEN_DIM, 0);
        lv_obj_set_style_bg_opa(t->bars[i], LV_OPA_COVER, 0);
    }

    ui_navbar_create(scr, UI_TAB_TELEMETRY, tab_cb);
    return t;
}

/* ------------------------------------------------------------------ setters */

void ui_telemetry_set_speed(ui_telemetry_t *t, float mph)
{
    if (t) lv_label_set_text_fmt(t->speed, "%.1f", mph);
}

void ui_telemetry_set_heading(ui_telemetry_t *t, int deg)
{
    if (t) lv_label_set_text_fmt(t->heading, "%03d\xC2\xB0", ((deg % 360) + 360) % 360);
}

void ui_telemetry_set_position(ui_telemetry_t *t, const char *ddm_lat,
                               const char *ddm_lon, double dd_lat, double dd_lon)
{
    if (!t) return;
    if (ddm_lat) lv_label_set_text(t->pos_line1, ddm_lat);
    if (ddm_lon) lv_label_set_text(t->pos_line2, ddm_lon);
    lv_label_set_text_fmt(t->pos_dd, "%.6f, %.6f | DD", dd_lat, dd_lon);
}

void ui_telemetry_set_altitude(ui_telemetry_t *t, int feet, int fpm)
{
    if (!t) return;
    lv_label_set_text_fmt(t->altitude, "%d ft", feet);
    lv_label_set_text_fmt(t->vspeed, "%+d fpm", fpm);
}

void ui_telemetry_set_quality(ui_telemetry_t *t, int used, int visible,
                              float hdop, float accuracy_ft)
{
    if (!t) return;
    lv_label_set_text_fmt(t->sats, "%d/%d", used, visible);
    lv_label_set_text_fmt(t->hdop, "%.1f", hdop);
    lv_obj_set_style_text_color(t->hdop, hdop <= 1.5f ? UI_C_GREEN : UI_C_RED, 0);
    lv_label_set_text_fmt(t->accuracy, "+/-%.1f", accuracy_ft);
}

void ui_telemetry_set_time(ui_telemetry_t *t, const char *local, const char *utc,
                           const char *tz_abbrev)
{
    if (!t) return;
    if (local)     lv_label_set_text(t->local_time, local);
    if (utc)       lv_label_set_text(t->utc_time, utc);
    if (tz_abbrev) lv_label_set_text_fmt(t->local_caption, "LOCAL | %s", tz_abbrev);
}

void ui_telemetry_set_trip(ui_telemetry_t *t, float miles, float max_mph,
                           const char *moving)
{
    if (!t) return;
    lv_label_set_text_fmt(t->trip, "%.2f", miles);
    lv_label_set_text_fmt(t->max_speed, "%.1f", max_mph);
    if (moving) lv_label_set_text(t->moving, moving);
}

void ui_telemetry_set_signal(ui_telemetry_t *t, const uint8_t *snr, int n,
                             int used, const char *constellations)
{
    if (!t) return;
    if (n > UI_TELEM_BARS) n = UI_TELEM_BARS;
    for (int i = 0; i < UI_TELEM_BARS; i++) {
        int v = (snr && i < n) ? snr[i] : 0;
        if (v > 55) v = 55;
        lv_obj_set_height(t->bars[i], LV_PCT(v * 100 / 55));
        lv_obj_set_style_bg_color(t->bars[i],
                                  v >= 25 ? UI_C_GREEN : UI_C_GREEN_DIM, 0);
    }
    lv_label_set_text_fmt(t->signal_caption, "SIGNAL - %d IN SOLUTION", used);
    if (constellations) lv_label_set_text(t->constellations, constellations);
}
