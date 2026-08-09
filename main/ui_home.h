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
    lv_obj_t *pos_acc;        /* ± 9.4 ft         */
    lv_obj_t *pos_alt;        /* 1,248 ft         */

    lv_obj_t *speed;
    lv_obj_t *heading_val;
    lv_obj_t *heading_sub;
    lv_obj_t *altitude;

    lv_obj_t *sat_count;
    lv_obj_t *sat_quality;
    lv_obj_t *sat_bars[4];
    lv_obj_t *acc_val;
    lv_obj_t *acc_quality;
    lv_obj_t *utc_time;
    lv_obj_t *utc_date;

    lv_obj_t *trip_distance;
    lv_obj_t *trip_moving;
    lv_obj_t *trip_avg;
    lv_obj_t *trip_max;
    lv_obj_t *trip_gain;

    lv_obj_t *track_btn;
    lv_obj_t *track_btn_label;
    bool tracking;
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
void ui_home_set_utc(ui_home_t *h, const char *hms, const char *date);
void ui_home_set_trip(ui_home_t *h, float distance_mi, const char *moving,
                      float avg_mph, float max_mph, int gain_ft);
void ui_home_set_tracking(ui_home_t *h, bool on);

#ifdef __cplusplus
}
#endif
#endif /* UI_HOME_H */
