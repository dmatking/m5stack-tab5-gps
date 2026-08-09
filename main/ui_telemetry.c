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
    ui_caption(pos, "POSITION \xC2\xB7 WGS84");
    t->pos_line1 = ui_label(pos, "32\xC2\xB0 54.1234' N", ui_font.semi_l, UI_C_TEXT);
    t->pos_line2 = ui_label(pos, "097\xC2\xB0 19.5678' W", ui_font.semi_l, UI_C_TEXT);
    t->pos_dd    = ui_label(pos, "32.902057, -97.326130 \xC2\xB7 DD", ui_font.s, UI_C_MUTED);

    lv_obj_t *r2 = row(body);
    cell(r2, 1, "ALTITUDE MSL",   "1,248 ft", ui_font.num_m, UI_C_TEXT, &t->altitude);
    cell(r2, 1, "VERTICAL SPEED", "+210 fpm", ui_font.num_m, UI_C_TEXT, &t->vspeed);

    lv_obj_t *r3 = row(body);
    cell(r3, 1, "SATELLITES", "14/19", ui_font.num_m, UI_C_TEXT,  &t->sats);
    cell(r3, 1, "HDOP",       "0.8",   ui_font.num_m, UI_C_GREEN, &t->hdop);
    // Split into adjacent literals ("\xB1" "9.4", not "\xB19.4") --
    // C's hex escapes are greedy and keep consuming hex digits, so
    // unsplit this parsed as the single (invalid, out-of-range) escape
    // \xB19 instead of \xB1 followed by the literal characters "9.4".
    cell(r3, 1, "ACCURACY",   "\xC2\xB1" "9.4", ui_font.num_m, UI_C_TEXT, &t->accuracy);

    lv_obj_t *r4 = row(body);
    cell(r4, 1, "LOCAL \xC2\xB7 CDT", "10:24:18", ui_font.num_m, UI_C_TEXT,  &t->local_time);
    cell(r4, 1, "UTC",                "15:24:18", ui_font.num_m, UI_C_MUTED, &t->utc_time);

    lv_obj_t *r5 = row(body);
    cell(r5, 1, "TRIP",      "12.48", ui_font.num_m, UI_C_TEXT, &t->trip);
    cell(r5, 1, "MAX SPEED", "68.3",  ui_font.num_m, UI_C_TEXT, &t->max_speed);
    cell(r5, 1, "MOVING",    "0:28",  ui_font.num_m, UI_C_TEXT, &t->moving);

    /* signal --------------------------------------------------------------- */
    lv_obj_t *sig = ui_card(body);
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
    t->signal_caption  = ui_caption(shead, "SIGNAL \xE2\x80\x94 14 IN SOLUTION");
    t->constellations  = ui_label(shead, "GPS \xC2\xB7 GLONASS \xC2\xB7 GALILEO",
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
    lv_label_set_text_fmt(t->pos_dd, "%.6f, %.6f \xC2\xB7 DD", dd_lat, dd_lon);
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
    lv_label_set_text_fmt(t->accuracy, "\xC2\xB1%.1f", accuracy_ft);
}

void ui_telemetry_set_time(ui_telemetry_t *t, const char *local, const char *utc)
{
    if (!t) return;
    if (local) lv_label_set_text(t->local_time, local);
    if (utc)   lv_label_set_text(t->utc_time, utc);
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
    lv_label_set_text_fmt(t->signal_caption, "SIGNAL \xE2\x80\x94 %d IN SOLUTION", used);
    if (constellations) lv_label_set_text(t->constellations, constellations);
}
