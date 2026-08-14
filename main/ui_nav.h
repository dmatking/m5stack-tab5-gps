/*
 * ui_nav.h — 2A Navigation (Goto active) screen
 */
#ifndef UI_NAV_H
#define UI_NAV_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *screen;
    ui_status_t status;

    lv_obj_t *dest_name;
    lv_obj_t *dest_meta;      /* "WPT 07 / 32° 59.6' N" */

    lv_obj_t *arrow_area;     /* square container holding the rose + arrow */
    lv_obj_t *arrow_shaft;
    lv_obj_t *arrow_barb_l;
    lv_obj_t *arrow_barb_r;
    lv_point_precise_t shaft_pts[2];
    lv_point_precise_t barb_l_pts[2];
    lv_point_precise_t barb_r_pts[2];

    lv_obj_t *brg;
    lv_obj_t *hdg;
    lv_obj_t *turn;

    lv_obj_t *distance;
    lv_obj_t *distance_unit;  /* "mi"/"km" */
    lv_obj_t *closure;
    lv_obj_t *closure_unit;   /* "mph"/"km/h" */
    lv_obj_t *eta;
    lv_obj_t *time_to_go;
    lv_obj_t *speed;
    lv_obj_t *speed_unit;     /* "mph"/"km/h" */

    lv_obj_t *xtk_label;      /* "0.12 mi left · closing" */
    lv_obj_t *xtk_dot;
    lv_obj_t *xtk_track;      /* rail the dot slides along */

    lv_obj_t *btn_map;
    lv_obj_t *btn_stop;
} ui_nav_t;

ui_nav_t *ui_nav_create(lv_event_cb_t tab_cb);

/* ---- setters ------------------------------------------------------------ */
void ui_nav_set_destination(ui_nav_t *n, const char *name, const char *meta);

/* Relative bearing: the arrow points at (bearing - heading). */
void ui_nav_set_bearing(ui_nav_t *n, int bearing_deg, int heading_deg);

// distance/vmg/speed are already converted; unit is "mi"/"km" (distance) or
// "mph"/"km/h" (closure, speed) per app_settings_get_distance_km().
void ui_nav_set_distance(ui_nav_t *n, float distance, const char *unit);
void ui_nav_set_closure(ui_nav_t *n, float vmg, const char *unit);
void ui_nav_set_eta(ui_nav_t *n, const char *eta_text, const char *time_to_go);
void ui_nav_set_speed(ui_nav_t *n, float speed, const char *unit);

/* offset_mi: negative = left of course, positive = right. Full-scale ±0.5 mi. */
void ui_nav_set_cross_track(ui_nav_t *n, float offset_mi, bool closing);

/* Attach handlers to the two footer buttons. */
void ui_nav_set_buttons(ui_nav_t *n, lv_event_cb_t map_cb, lv_event_cb_t stop_cb);

#ifdef __cplusplus
}
#endif
#endif /* UI_NAV_H */
