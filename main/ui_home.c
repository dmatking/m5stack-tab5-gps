#include "ui_home.h"

static ui_home_t s_home;

/* A metric card: caption on top, big value, unit under it. top_pad is extra
 * space inserted between caption and value -- SPEED/ALTITUDE pass the
 * offset that lines their value up with HEADING's compass dial (ui_compass()
 * centers its own number well below the caption, inside the dial), without
 * moving the caption itself. 0 for a plain top-anchored card. */
static lv_obj_t *metric_card(lv_obj_t *parent, int grow, const char *caption,
                             const char *value, const lv_font_t *value_font,
                             const char *unit, lv_color_t value_color,
                             lv_coord_t top_pad, lv_obj_t **out_value,
                             lv_obj_t **out_unit)
{
    lv_obj_t *c = ui_card(parent);
    lv_obj_set_height(c, LV_PCT(100));
    lv_obj_set_flex_grow(c, grow);
    ui_flex_col(c, 4);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_caption(c, caption);
    if (top_pad > 0) {
        lv_obj_t *spacer = ui_box(c);
        lv_obj_set_size(spacer, 1, top_pad);
    }
    lv_obj_t *v = ui_label(c, value, value_font, value_color);
    if (out_value) *out_value = v;
    if (unit) {
        lv_obj_t *u = ui_label(c, unit, ui_font.s, UI_C_MUTED);
        if (out_unit) *out_unit = u;
    }
    return c;
}

/* One column of the trip strip. one_line_caption is true for DISTANCE/
 * MOVING -- their captions are one line while AVG/MAX SPEED and ELEV GAIN's
 * are two ("SPEED\nAVG" etc.), and without this their values sit visibly
 * higher, out of line with the other three (confirmed via a photographed-
 * alignment comparison). Adds a same-size-as-a-caption-line spacer instead
 * of touching the caption itself, so the titles stay put and only the
 * value/unit move down to match. */
static void trip_cell(lv_obj_t *parent, const char *caption, const char *value,
                      const char *unit, bool left_rule, bool one_line_caption,
                      lv_obj_t **out_value, lv_obj_t **out_unit)
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
    lv_obj_t *cap = ui_caption(cell, caption);
    // The cell's own flex align only centers the caption *label* as a
    // block; a two-line caption (e.g. "SPEED\nAVG") auto-sizes to its
    // widest line and left-aligns the shorter one under it by default,
    // which reads as off-center. Needed even for single-line captions'
    // sake of not special-casing this per call -- harmless there since a
    // single line has nothing to center against.
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
    if (one_line_caption) {
        lv_obj_t *spacer = ui_box(cell);
        lv_obj_set_size(spacer, 1, 22); // ~one ui_font.xs line, matched against a real capture
    }
    *out_value = ui_label(cell, value, ui_font.semi_m, UI_C_TEXT);
    lv_obj_t *u = ui_label(cell, unit, ui_font.xs, UI_C_MUTED);
    if (out_unit) *out_unit = u;
}

static void track_btn_cb(lv_event_t *e)
{
    ui_home_t *h = (ui_home_t *)lv_event_get_user_data(e);
    ui_home_set_tracking(h, !h->tracking);
}

/* Reset Trip confirm dialog -- a scrim + small card, built fresh on demand
 * and torn down on either button rather than pre-built/hidden. Only one can
 * ever be open at a time (the Reset Trip button is the only thing that
 * opens it), so a single file-static handle is enough to track it. */
static lv_obj_t *s_reset_confirm;

static void reset_confirm_close(void)
{
    if (s_reset_confirm) {
        lv_obj_delete(s_reset_confirm);
        s_reset_confirm = NULL;
    }
}

static void reset_confirm_no_cb(lv_event_t *e)
{
    (void)e;
    reset_confirm_close();
}

static void reset_confirm_yes_cb(lv_event_t *e)
{
    ui_home_t *h = (ui_home_t *)lv_event_get_user_data(e);
    if (h->reset_trip_cb) h->reset_trip_cb(); // zeroes gps_ui_bridge.c's real accumulator
    // Instant feedback -- the next tick would repaint these anyway once the
    // accumulator above is actually zeroed, but no reason to make the user
    // wait up to a tick period to see it took effect. Units unchanged, so
    // pass NULL for all three unit strings rather than re-deriving them here.
    ui_home_set_trip(h, 0.0f, "0:00:00", 0.0f, 0.0f, 0, NULL, NULL, NULL);
    reset_confirm_close();
}

static void reset_trip_btn_cb(lv_event_t *e)
{
    ui_home_t *h = (ui_home_t *)lv_event_get_user_data(e);
    if (s_reset_confirm) return; // already open

    // Full-screen dim scrim, added as the screen's last (topmost) child --
    // clickable so it absorbs taps on anything behind it, same effect as a
    // modal without needing a separate top layer. h->screen lays out its
    // children (body + navbar) in a flex column, so without FLOATING this
    // would just become a 3rd flex item positioned *after* the navbar
    // instead of covering it -- confirmed on real hardware: the navbar
    // stayed fully visible/tappable (including switching tabs right out
    // from under an open confirm dialog) until this was added.
    lv_obj_t *scrim = ui_box(h->screen);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_FLOATING); // skip the screen's flex layout
    lv_obj_set_pos(scrim, 0, 0);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    s_reset_confirm = scrim;

    lv_obj_t *card = ui_card(scrim);
    lv_obj_set_width(card, LV_PCT(80));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 20, 0);
    ui_flex_col(card, 16);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_label(card, "Reset trip data?", ui_font.semi_m, UI_C_TEXT);
    lv_obj_t *desc = ui_label(card, "Distance, moving time, and speed/elevation totals go back to zero.",
                              ui_font.s, UI_C_MUTED);
    lv_obj_set_width(desc, LV_PCT(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btns = ui_box(card);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(btns, 12);
    lv_obj_t *bl = ui_box(btns);
    lv_obj_set_size(bl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bl, 1);
    lv_obj_t *no_btn = ui_button(bl, "Cancel", UI_C_CARD, UI_C_MUTED, true, UI_C_BORDER, 64);
    lv_obj_add_event_cb(no_btn, reset_confirm_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *br = ui_box(btns);
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(br, 1);
    lv_obj_t *yes_btn = ui_button(br, "Reset", UI_C_CARD, UI_C_RED, true, lv_color_hex(0x63303A), 64);
    lv_obj_add_event_cb(yes_btn, reset_confirm_yes_cb, LV_EVENT_CLICKED, h);
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

    ui_status_create(scr, &h->status);

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

    // No accuracy/altitude sub-line here anymore -- it duplicated the
    // ALTITUDE card and GPS ACCURACY card in the row below with no added
    // info, just noise. ui_home_set_accuracy()/ui_home_set_altitude() used
    // to update both; now just the cards.

    /* speed / heading / altitude ----------------------------------------- */
    lv_obj_t *row1 = ui_box(body);
    lv_obj_set_width(row1, LV_PCT(100));
    lv_obj_set_height(row1, 262);
    ui_flex_row(row1, 12);

    // 49px top_pad on SPEED/ALTITUDE: HEADING's ui_compass() centers its
    // "000" well below the caption, inside the 180px dial -- without this,
    // SPEED/ALTITUDE's numbers sit noticeably higher than HEADING's,
    // confirmed via a photographed-alignment comparison. Not derived from
    // exact font metrics, just measured/adjusted against a real capture.
    metric_card(row1, 10, "SPEED", "42", ui_font.num_l, "mph", UI_C_TEXT,
                49, &h->speed, &h->speed_unit);

    lv_obj_t *hcard = ui_card(row1);
    lv_obj_set_height(hcard, LV_PCT(100));
    lv_obj_set_flex_grow(hcard, 13);
    ui_flex_col(hcard, 4);
    lv_obj_set_flex_align(hcard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(hcard, "HEADING");
    ui_compass(hcard, 180, &h->heading_val, &h->heading_sub);

    metric_card(row1, 10, "ALTITUDE", "1,248", ui_font.num_l, "ft", UI_C_TEXT,
                49, &h->altitude, &h->altitude_unit);

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
    // Was a 4-bar phone-signal-style meter -- at 52px tall, noticeably
    // taller than GPS ACCURACY's LV_SYMBOL_GPS glyph and TIME's 32px clock
    // icon in the same row, which pushed this card's count down out of line
    // with theirs (confirmed via a photographed-alignment comparison). A
    // satellite icon at the same 32px size as the clock icon fixes the
    // alignment and reads more literally as "satellites" anyway.
    ui_satellite_icon(sat, 32, UI_C_GREEN);
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
    // A clock face, not LV_SYMBOL_REFRESH -- that was never the right icon
    // for this card, just the closest stock LVGL symbol at hand. LVGL's
    // symbol font has no clock glyph at all, so this is hand-drawn (see
    // ui_theme.c's ui_clock_icon()) rather than a font character like the
    // GPS_ACCURACY card's icon just above.
    ui_clock_icon(utc, 32, UI_C_GREEN);
    h->utc_time = ui_label(utc, "15:24:18", ui_font.semi_l, UI_C_TEXT);
    // AM/PM on its own (smaller) line rather than appended to utc_time --
    // "3:24:18 PM" inline was wide enough to clip against this card's
    // edges in 12-hour mode. Hidden entirely in 24-hour mode (no AM/PM to
    // show) rather than left blank, so it doesn't leave a gap in the
    // layout -- see ui_home_set_local_time().
    h->utc_ampm = ui_label(utc, "PM", ui_font.s, UI_C_MUTED);
    h->utc_date = ui_label(utc, "May 26, 2025", ui_font.s, UI_C_MUTED);

    /* trip strip ---------------------------------------------------------- */
    // No ui_mark_placeholder() -- real data now, fed by gps_ui_bridge.c's
    // trip accumulator (see its own comment for the moving-gated distance/
    // elevation-gain logic).
    lv_obj_t *trip = ui_card(body);
    lv_obj_set_width(trip, LV_PCT(100));
    lv_obj_set_height(trip, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(trip, 18, 0);
    ui_flex_col(trip, 14);
    lv_obj_set_flex_align(trip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(trip, "TRIP (Since Reset)");

    lv_obj_t *cells = ui_box(trip);
    lv_obj_set_size(cells, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(cells, 0);
    trip_cell(cells, "DISTANCE",    "12.48",   "mi",    false, true,  &h->trip_distance, &h->trip_distance_unit);
    trip_cell(cells, "MOVING",      "0:28:47", "h:m:s", true,  true,  &h->trip_moving, NULL);
    // Two lines each (was one, cramped) -- same words, just wrapped, not
    // abbreviated. SPEED first (not AVG/MAX first) so the shared word lines
    // up between these two adjacent cells, with the distinguishing word
    // underneath.
    trip_cell(cells, "SPEED\nAVG",  "25.9",    "mph",   true,  false, &h->trip_avg, &h->trip_avg_unit);
    trip_cell(cells, "SPEED\nMAX",  "68.3",    "mph",   true,  false, &h->trip_max, &h->trip_max_unit);
    trip_cell(cells, "ELEV\nGAIN",  "512",     "ft",    true,  false, &h->trip_gain, &h->trip_gain_unit);

    /* Below the card, not inside it -- same footprint as Start Tracking
     * below, just a secondary/destructive action (outline + red, matching
     * ui_nav.c's "Stop Nav" button) rather than the primary blue fill. */
    lv_obj_t *reset_btn = ui_button(body, "Reset Trip", UI_C_CARD, UI_C_RED,
                                    true, lv_color_hex(0x63303A), 64);
    lv_obj_add_event_cb(reset_btn, reset_trip_btn_cb, LV_EVENT_CLICKED, h);

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

void ui_home_set_accuracy(ui_home_t *h, float value, const char *unit)
{
    if (!h) return;
    lv_label_set_text_fmt(h->acc_val, "+/- %.1f %s", value, unit ? unit : "ft");
    // Quality thresholds are unit-specific -- 16ft/40ft in feet, converted
    // to their meter equivalents (~4.9m/~12.2m) when in meters, so "Good"/
    // "Fair"/"Poor" mean the same real-world accuracy either way.
    bool is_m = unit && unit[0] == 'm' && unit[1] == '\0';
    float good = is_m ? 4.9f : 16.0f;
    float fair = is_m ? 12.2f : 40.0f;
    const char *q = value <= good ? "Good" : (value <= fair ? "Fair" : "Poor");
    lv_label_set_text(h->acc_quality, q);
    lv_obj_set_style_text_color(h->acc_quality,
                                value <= good ? UI_C_MUTED : UI_C_RED, 0);
}

void ui_home_set_speed(ui_home_t *h, float speed, const char *unit)
{
    if (!h) return;
    lv_label_set_text_fmt(h->speed, "%d", (int)(speed + 0.5f));
    if (unit) lv_label_set_text(h->speed_unit, unit);
}

void ui_home_set_heading(ui_home_t *h, int deg, const char *cardinal, bool valid)
{
    if (h) ui_compass_set_heading(h->heading_val, h->heading_sub, deg, cardinal, valid);
}

void ui_home_set_altitude(ui_home_t *h, int altitude, const char *unit)
{
    if (!h) return;
    lv_label_set_text_fmt(h->altitude, "%d", altitude);
    if (unit) lv_label_set_text(h->altitude_unit, unit);
}

void ui_home_set_satellites(ui_home_t *h, int count, const char *quality)
{
    if (!h) return;
    lv_label_set_text_fmt(h->sat_count, "%d", count);
    if (quality) lv_label_set_text(h->sat_quality, quality);
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

void ui_home_set_trip(ui_home_t *h, float distance, const char *moving,
                      float avg, float max, int gain,
                      const char *dist_unit, const char *speed_unit,
                      const char *elev_unit)
{
    if (!h) return;
    lv_label_set_text_fmt(h->trip_distance, "%.2f", distance);
    if (dist_unit) lv_label_set_text(h->trip_distance_unit, dist_unit);
    if (moving) lv_label_set_text(h->trip_moving, moving);
    lv_label_set_text_fmt(h->trip_avg, "%.1f", avg);
    lv_label_set_text_fmt(h->trip_max, "%.1f", max);
    if (speed_unit) {
        lv_label_set_text(h->trip_avg_unit, speed_unit);
        lv_label_set_text(h->trip_max_unit, speed_unit);
    }
    lv_label_set_text_fmt(h->trip_gain, "%d", gain);
    if (elev_unit) lv_label_set_text(h->trip_gain_unit, elev_unit);
}

void ui_home_set_tracking(ui_home_t *h, bool on)
{
    if (!h) return;
    h->tracking = on;
    lv_label_set_text(h->track_btn_label, on ? "Stop Tracking" : "Start Tracking");
    lv_obj_set_style_bg_color(h->track_btn, on ? UI_C_GREEN_DIM : UI_C_BLUE_BTN, 0);
}

void ui_home_set_reset_trip_cb(ui_home_t *h, void (*cb)(void))
{
    if (h) h->reset_trip_cb = cb;
}
