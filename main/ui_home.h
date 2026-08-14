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
    lv_obj_t *heading_val;
    lv_obj_t *heading_sub;
    lv_obj_t *altitude;

    lv_obj_t *sat_count;
    lv_obj_t *sat_quality;
    lv_obj_t *acc_val;
    lv_obj_t *acc_quality;
    lv_obj_t *time_caption;   /* "TIME (CDT)"/"TIME (CST)" -- see ui_home_set_local_time() */
    lv_obj_t *utc_time;
    lv_obj_t *utc_ampm;       /* own line, hidden entirely in 24-hour mode */
    lv_obj_t *utc_date;

    lv_obj_t *trip_distance;
    lv_obj_t *trip_moving;
    lv_obj_t *trip_avg;
    lv_obj_t *trip_max;
    lv_obj_t *trip_gain;

    lv_obj_t *track_btn;
    lv_obj_t *track_btn_label;
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
void ui_home_set_accuracy(ui_home_t *h, float feet);
void ui_home_set_speed(ui_home_t *h, float mph);
void ui_home_set_heading(ui_home_t *h, int deg, const char *cardinal);
void ui_home_set_altitude(ui_home_t *h, int feet);
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
void ui_home_set_trip(ui_home_t *h, float distance_mi, const char *moving,
                      float avg_mph, float max_mph, int gain_ft);
void ui_home_set_tracking(ui_home_t *h, bool on);

// Registers the callback the Reset Trip button fires once the user confirms
// the yes/no prompt -- see ui_home_t's reset_trip_cb field. Optional: if
// never set, the button still resets this screen's own displayed values
// but nothing upstream keeps re-feeding it real ones.
void ui_home_set_reset_trip_cb(ui_home_t *h, void (*cb)(void));

#ifdef __cplusplus
}
#endif
#endif /* UI_HOME_H */
