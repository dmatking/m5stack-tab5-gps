/*
 * ui_home.h — 2A Home / dashboard screen
 */
#ifndef UI_HOME_H
#define UI_HOME_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *screen;
    ui_status_t status;

    lv_obj_t *pos_line1;      /* 32° 54.1234' N   */
    lv_obj_t *pos_line2;      /* 097° 19.5678' W  */

    lv_obj_t *speed;
    lv_obj_t *speed_unit;     /* "mph"/"km/h" -- see app_settings_get_distance_km() */
    lv_obj_t *heading_val;
    lv_obj_t *heading_sub;
    lv_obj_t *altitude;
    lv_obj_t *altitude_unit;  /* "ft"/"m" -- see app_settings_get_elevation_m() */

    lv_obj_t *sat_count;
    lv_obj_t *sat_quality;
    lv_obj_t *acc_val;
    lv_obj_t *acc_quality;
    lv_obj_t *time_caption;   /* "TIME (CDT)"/"TIME (CST)" -- see ui_home_set_local_time() */
    lv_obj_t *utc_time;
    lv_obj_t *utc_ampm;       /* own line, hidden entirely in 24-hour mode */
    lv_obj_t *utc_date;

    lv_obj_t *trip_distance;
    lv_obj_t *trip_distance_unit; /* "mi"/"km" */
    lv_obj_t *trip_moving;
    lv_obj_t *trip_avg;
    lv_obj_t *trip_avg_unit;      /* "mph"/"km/h" */
    lv_obj_t *trip_max;
    lv_obj_t *trip_max_unit;      /* "mph"/"km/h" */
    lv_obj_t *trip_gain;
    lv_obj_t *trip_gain_unit;     /* "ft"/"m" */

    lv_obj_t *track_btn;
    lv_obj_t *track_btn_label;
    lv_obj_t *mark_btn;
    lv_obj_t *mark_btn_label;   /* label swapped for ~1.8s by ui_home_flash_mark() */
    bool tracking;

    // Set by gps_ui_bridge.c via ui_home_set_reset_trip_cb() so the Reset
    // Trip button (behind its own yes/no confirm) can zero the *real*
    // accumulator living in gps_ui_bridge.c, not just this screen's
    // displayed text -- otherwise the very next tick would overwrite the
    // reset with the old accumulated values again.
    void (*reset_trip_cb)(void);
} ui_home_t;

/* Builds the screen but does not load it. `tab_cb` is forwarded to the tab bar. */
ui_home_t *ui_home_create(lv_event_cb_t tab_cb);

/* ---- setters ------------------------------------------------------------ */
void ui_home_set_position(ui_home_t *h, const char *lat, const char *lon);
// value/unit are already converted -- e.g. (9.4, "ft") or (2.9, "m"), per
// app_settings_get_elevation_m(); this screen just displays what it's given.
// valid == false (no fix yet this session) shows "--" instead of `value` --
// see ui_home_set_speed()'s own comment for why this matters: unlike
// ui_home_set_heading() below, this used to have no way to express "no
// data" at all, so a device that never got a fix showed this card's
// creation-time demo number forever instead.
void ui_home_set_accuracy(ui_home_t *h, float value, const char *unit, bool valid);
// valid == false (no fix yet this session) shows "--" instead of `speed` --
// gps_ui_bridge.c's tick() only ever called this when st.speed_valid, so a
// device that never got a fix (confirmed on real hardware: NO FIX/0 sats
// sitting right next to a confident "42 mph") kept showing
// ui_home_create()'s literal creation-time demo number forever, since
// nothing ever told this card there was no real data to show instead.
// ui_home_set_heading() right below already had this exact problem solved
// (its own `valid` param) -- this brings speed/altitude/accuracy/position/
// local-time in line with it rather than leaving them as the one part of
// this screen that couldn't say "no data yet".
void ui_home_set_speed(ui_home_t *h, float speed, const char *unit, bool valid);
// valid == false (stationary, or no fix) shows "---"/"--" -- see gps.h's
// heading_valid for why a GPS course-over-ground can't be trusted at rest.
void ui_home_set_heading(ui_home_t *h, int deg, const char *cardinal, bool valid);
void ui_home_set_altitude(ui_home_t *h, int altitude, const char *unit, bool valid);
void ui_home_set_satellites(ui_home_t *h, int count, const char *quality);
// Despite the original design's card being captioned "TIME (UTC)", this
// project's users are all in one place (Fort Worth, TX) and want to read
// local time at a glance, not do UTC arithmetic in their head -- feeds
// US Central time (see gps_ui_bridge.c's us_central_from_utc()), and
// updates the card's own caption to say which one (CDT/CST) is currently
// in effect, since that flips twice a year.
// ampm is "AM"/"PM" (own line, smaller font -- inline "3:24:18 PM" used to
// clip against the card's edges) or NULL/"" in 24-hour mode, which hides
// that line entirely rather than leaving it blank.
void ui_home_set_local_time(ui_home_t *h, const char *hms, const char *ampm,
                            const char *date, const char *tz_abbrev);
// distance/avg/max/gain are already converted; dist_unit ("mi"/"km"),
// speed_unit ("mph"/"km/h"), and elev_unit ("ft"/"m") label them.
void ui_home_set_trip(ui_home_t *h, float distance, const char *moving,
                      float avg, float max, int gain,
                      const char *dist_unit, const char *speed_unit,
                      const char *elev_unit);
void ui_home_set_tracking(ui_home_t *h, bool on);

// Registers the callback the Reset Trip button fires once the user confirms
// the yes/no prompt -- see ui_home_t's reset_trip_cb field. Optional: if
// never set, the button still resets this screen's own displayed values
// but nothing upstream keeps re-feeding it real ones.
void ui_home_set_reset_trip_cb(ui_home_t *h, void (*cb)(void));

// Same split as reset-trip: the Mark Position handler lives in
// gps_ui_bridge.c because it needs the live GPS position, which this file
// has no access to.
void ui_home_set_mark_cb(ui_home_t *h, void (*cb)(void));

// Briefly replaces the Mark Position button's own label with `text` (the
// new waypoint's name, or why it failed), then restores it after ~1.8s.
// Stands in for a toast/snackbar, which this app doesn't have.
void ui_home_flash_mark(ui_home_t *h, const char *text);

#ifdef __cplusplus
}
#endif
#endif /* UI_HOME_H */
