#include "ui_home.h"

static ui_home_t s_home;

/* A metric card: caption on top, big value, unit under it. */
static lv_obj_t *metric_card(lv_obj_t *parent, int grow, const char *caption,
                             const char *value, const lv_font_t *value_font,
                             const char *unit, lv_color_t value_color,
                             lv_obj_t **out_value)
{
    lv_obj_t *c = ui_card(parent);
    lv_obj_set_height(c, LV_PCT(100));
    lv_obj_set_flex_grow(c, grow);
    ui_flex_col(c, 4);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_caption(c, caption);
    lv_obj_t *v = ui_label(c, value, value_font, value_color);
    if (out_value) *out_value = v;
    if (unit) ui_label(c, unit, ui_font.s, UI_C_MUTED);
    return c;
}

/* One column of the trip strip. */
static void trip_cell(lv_obj_t *parent, const char *caption, const char *value,
                      const char *unit, bool left_rule, lv_obj_t **out_value)
{
    lv_obj_t *cell = ui_box(parent);
    lv_obj_set_height(cell, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(cell, 1);
    ui_flex_col(cell, 2);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    if (left_rule) {
        lv_obj_set_style_border_color(cell, UI_C_BORDER, 0);
        lv_obj_set_style_border_width(cell, 1, 0);
        lv_obj_set_style_border_side(cell, LV_BORDER_SIDE_LEFT, 0);
    }
    ui_caption(cell, caption);
    *out_value = ui_label(cell, value, ui_font.semi_m, UI_C_TEXT);
    ui_label(cell, unit, ui_font.xs, UI_C_MUTED);
}

static void track_btn_cb(lv_event_t *e)
{
    ui_home_t *h = (ui_home_t *)lv_event_get_user_data(e);
    ui_home_set_tracking(h, !h->tracking);
}

ui_home_t *ui_home_create(lv_event_cb_t tab_cb)
{
    ui_home_t *h = &s_home;
    lv_memzero(h, sizeof(*h));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    h->screen = scr;

    ui_status_create(scr, &h->status, false);

    /* content column ----------------------------------------------------- */
    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 14, 0);
    ui_flex_col(body, UI_GAP);

    /* current position --------------------------------------------------- */
    lv_obj_t *pos = ui_card(body);
    lv_obj_set_width(pos, LV_PCT(100));
    lv_obj_set_height(pos, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_ver(pos, 18, 0);
    ui_flex_col(pos, 6);
    lv_obj_set_flex_align(pos, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_caption(pos, "CURRENT POSITION");
    h->pos_line1 = ui_label(pos, "32\xC2\xB0 54.1234' N", ui_font.semi_l, UI_C_TEXT);
    h->pos_line2 = ui_label(pos, "097\xC2\xB0 19.5678' W", ui_font.semi_l, UI_C_TEXT);

    lv_obj_t *pos_sub = ui_box(pos);
    lv_obj_set_size(pos_sub, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(pos_sub, 20);
    lv_obj_set_flex_align(pos_sub, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    // "+/-" not "\xC2\xB1" (±) -- LVGL's built-in Montserrat fonts only
    // include ASCII plus the degree sign, nothing else outside that; ± (and
    // every other non-ASCII punctuation this design used) rendered as a
    // tofu box on real hardware. Confirmed via a real ± usage elsewhere in
    // this same file/other UI files (photographed on the actual panel).
    h->pos_acc = ui_label(pos_sub, LV_SYMBOL_WARNING " +/- 9.4 ft", ui_font.s, UI_C_MUTED);
    lv_obj_t *bar = ui_box(pos_sub);
    lv_obj_set_size(bar, 1, 24);
    lv_obj_set_style_bg_color(bar, UI_C_BORDER, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    h->pos_alt = ui_label(pos_sub, "1,248 ft", ui_font.s, UI_C_MUTED);

    /* speed / heading / altitude ----------------------------------------- */
    lv_obj_t *row1 = ui_box(body);
    lv_obj_set_width(row1, LV_PCT(100));
    lv_obj_set_height(row1, 262);
    ui_flex_row(row1, 12);

    metric_card(row1, 10, "SPEED", "42", ui_font.num_l, "mph", UI_C_TEXT,
                &h->speed);

    lv_obj_t *hcard = ui_card(row1);
    lv_obj_set_height(hcard, LV_PCT(100));
    lv_obj_set_flex_grow(hcard, 13);
    ui_flex_col(hcard, 4);
    lv_obj_set_flex_align(hcard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(hcard, "HEADING");
    ui_compass(hcard, 180, &h->heading_val, &h->heading_sub);

    metric_card(row1, 10, "ALTITUDE", "1,248", ui_font.num_l, "ft", UI_C_TEXT,
                &h->altitude);

    /* satellites / accuracy / time --------------------------------------- */
    lv_obj_t *row2 = ui_box(body);
    lv_obj_set_width(row2, LV_PCT(100));
    lv_obj_set_height(row2, 215);
    ui_flex_row(row2, 12);

    lv_obj_t *sat = ui_card(row2);
    lv_obj_set_height(sat, LV_PCT(100));
    lv_obj_set_flex_grow(sat, 10);
    ui_flex_col(sat, 8);
    lv_obj_set_flex_align(sat, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(sat, "SATELLITES");
    lv_obj_t *bars = ui_box(sat);
    lv_obj_set_size(bars, LV_SIZE_CONTENT, 52);
    ui_flex_row(bars, 6);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    const int bar_pct[4] = { 36, 58, 78, 100 };
    for (int i = 0; i < 4; i++) {
        h->sat_bars[i] = ui_box(bars);
        lv_obj_set_size(h->sat_bars[i], 13, LV_PCT(bar_pct[i]));
        lv_obj_set_style_radius(h->sat_bars[i], 2, 0);
        lv_obj_set_style_bg_color(h->sat_bars[i], UI_C_GREEN, 0);
        lv_obj_set_style_bg_opa(h->sat_bars[i], LV_OPA_COVER, 0);
    }
    h->sat_count   = ui_label(sat, "14", ui_font.num_m, UI_C_TEXT);
    h->sat_quality = ui_label(sat, "Good", ui_font.s, UI_C_MUTED);

    lv_obj_t *acc = ui_card(row2);
    lv_obj_set_height(acc, LV_PCT(100));
    lv_obj_set_flex_grow(acc, 13);
    ui_flex_col(acc, 8);
    lv_obj_set_flex_align(acc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(acc, "GPS ACCURACY");
    ui_label(acc, LV_SYMBOL_GPS, ui_font.semi_m, UI_C_GREEN);
    h->acc_val     = ui_label(acc, "+/- 9.4 ft", ui_font.num_m, UI_C_TEXT);
    h->acc_quality = ui_label(acc, "Good", ui_font.s, UI_C_MUTED);

    lv_obj_t *utc = ui_card(row2);
    lv_obj_set_height(utc, LV_PCT(100));
    lv_obj_set_flex_grow(utc, 10);
    ui_flex_col(utc, 8);
    lv_obj_set_flex_align(utc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    // Demo caption says CDT (matching the demo "10:24 AM"-ish daytime
    // scenario the mockup implies) -- ui_home_set_local_time() below keeps
    // it in sync with whichever of CDT/CST is actually in effect once real
    // data arrives.
    h->time_caption = ui_caption(utc, "TIME (CDT)");
    ui_label(utc, LV_SYMBOL_REFRESH, ui_font.semi_m, UI_C_GREEN);
    h->utc_time = ui_label(utc, "15:24:18", ui_font.semi_l, UI_C_TEXT);
    // AM/PM on its own (smaller) line rather than appended to utc_time --
    // "3:24:18 PM" inline was wide enough to clip against this card's
    // edges in 12-hour mode. Hidden entirely in 24-hour mode (no AM/PM to
    // show) rather than left blank, so it doesn't leave a gap in the
    // layout -- see ui_home_set_local_time().
    h->utc_ampm = ui_label(utc, "PM", ui_font.s, UI_C_MUTED);
    h->utc_date = ui_label(utc, "May 26, 2025", ui_font.s, UI_C_MUTED);

    /* trip strip ---------------------------------------------------------- */
    lv_obj_t *trip = ui_card(body);
    lv_obj_set_width(trip, LV_PCT(100));
    lv_obj_set_height(trip, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(trip, 14, 0);
    ui_flex_col(trip, 10);
    lv_obj_set_flex_align(trip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(trip, "TRIP (Since Reset)");

    lv_obj_t *cells = ui_box(trip);
    lv_obj_set_size(cells, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(cells, 0);
    trip_cell(cells, "DISTANCE", "12.48",   "mi",    false, &h->trip_distance);
    trip_cell(cells, "MOVING",   "0:28:47", "h:m:s", true,  &h->trip_moving);
    trip_cell(cells, "AVG SPEED","25.9",    "mph",   true,  &h->trip_avg);
    trip_cell(cells, "MAX SPEED","68.3",    "mph",   true,  &h->trip_max);
    trip_cell(cells, "ELEV GAIN","512",     "ft",    true,  &h->trip_gain);

    /* spacer + primary action -------------------------------------------- */
    lv_obj_t *spacer = ui_box(body);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    h->track_btn = ui_button(body, "Start Tracking", UI_C_BLUE_BTN, UI_C_TEXT,
                             false, UI_C_BLUE, 96);
    h->track_btn_label = lv_obj_get_child(h->track_btn, 0);
    lv_obj_add_event_cb(h->track_btn, track_btn_cb, LV_EVENT_CLICKED, h);

    ui_navbar_create(scr, UI_TAB_HOME, tab_cb);
    return h;
}

/* ------------------------------------------------------------------ setters */

void ui_home_set_position(ui_home_t *h, const char *lat, const char *lon)
{
    if (!h) return;
    lv_label_set_text(h->pos_line1, lat);
    lv_label_set_text(h->pos_line2, lon);
}

void ui_home_set_accuracy(ui_home_t *h, float feet)
{
    if (!h) return;
    lv_label_set_text_fmt(h->pos_acc, "+/- %.1f ft", feet);
    lv_label_set_text_fmt(h->acc_val, "+/- %.1f ft", feet);
    const char *q = feet <= 16.0f ? "Good" : (feet <= 40.0f ? "Fair" : "Poor");
    lv_label_set_text(h->acc_quality, q);
    lv_obj_set_style_text_color(h->acc_quality,
                                feet <= 16.0f ? UI_C_MUTED : UI_C_RED, 0);
}

void ui_home_set_speed(ui_home_t *h, float mph)
{
    if (h) lv_label_set_text_fmt(h->speed, "%d", (int)(mph + 0.5f));
}

void ui_home_set_heading(ui_home_t *h, int deg, const char *cardinal)
{
    if (h) ui_compass_set_heading(h->heading_val, h->heading_sub, deg, cardinal);
}

void ui_home_set_altitude(ui_home_t *h, int feet)
{
    if (!h) return;
    lv_label_set_text_fmt(h->altitude, "%d", feet);
    lv_label_set_text_fmt(h->pos_alt, "%d ft", feet);
}

void ui_home_set_satellites(ui_home_t *h, int count, const char *quality)
{
    if (!h) return;
    lv_label_set_text_fmt(h->sat_count, "%d", count);
    if (quality) lv_label_set_text(h->sat_quality, quality);
    for (int i = 0; i < 4; i++) {
        bool lit = count >= (i + 1) * 4;
        lv_obj_set_style_bg_color(h->sat_bars[i],
                                  lit ? UI_C_GREEN : UI_C_GREEN_DIM, 0);
    }
}

void ui_home_set_local_time(ui_home_t *h, const char *hms, const char *ampm,
                            const char *date, const char *tz_abbrev)
{
    if (!h) return;
    if (hms)       lv_label_set_text(h->utc_time, hms);
    if (date)      lv_label_set_text(h->utc_date, date);
    if (tz_abbrev) lv_label_set_text_fmt(h->time_caption, "TIME (%s)", tz_abbrev);
    // NULL/empty (24-hour mode -- no AM/PM to show) hides the row entirely
    // rather than leaving it blank, so it doesn't leave a gap in the card.
    if (ampm && *ampm) {
        lv_label_set_text(h->utc_ampm, ampm);
        lv_obj_remove_flag(h->utc_ampm, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(h->utc_ampm, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_home_set_trip(ui_home_t *h, float distance_mi, const char *moving,
                      float avg_mph, float max_mph, int gain_ft)
{
    if (!h) return;
    lv_label_set_text_fmt(h->trip_distance, "%.2f", distance_mi);
    if (moving) lv_label_set_text(h->trip_moving, moving);
    lv_label_set_text_fmt(h->trip_avg, "%.1f", avg_mph);
    lv_label_set_text_fmt(h->trip_max, "%.1f", max_mph);
    lv_label_set_text_fmt(h->trip_gain, "%d", gain_ft);
}

void ui_home_set_tracking(ui_home_t *h, bool on)
{
    if (!h) return;
    h->tracking = on;
    lv_label_set_text(h->track_btn_label, on ? "Stop Tracking" : "Start Tracking");
    lv_obj_set_style_bg_color(h->track_btn, on ? UI_C_GREEN_DIM : UI_C_BLUE_BTN, 0);
}
