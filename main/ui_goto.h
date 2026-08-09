/*
 * ui_goto.h — 2A Goto screen: coordinate entry + saved waypoints
 */
#ifndef UI_GOTO_H
#define UI_GOTO_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_COORD_DDM = 0,   /* DD MM.MMMM  (default) */
    UI_COORD_DD,        /* DD.DDDDDD             */
    UI_COORD_DMS        /* DD MM SS              */
} ui_coord_fmt_t;

typedef enum { UI_GOTO_FIELD_LAT = 0, UI_GOTO_FIELD_LON } ui_goto_field_t;

typedef struct {
    lv_obj_t *screen;
    ui_status_t status;

    lv_obj_t *tab_entry;
    lv_obj_t *tab_saved;

    lv_obj_t *fmt_btn[3];
    ui_coord_fmt_t fmt;

    lv_obj_t *lat_card;
    lv_obj_t *lat_value;
    lv_obj_t *lat_hemi;
    lv_obj_t *lon_card;
    lv_obj_t *lon_value;
    lv_obj_t *lon_hemi;

    lv_obj_t *recent_name;
    lv_obj_t *recent_meta;

    lv_obj_t *btn_cancel;
    lv_obj_t *btn_start;

    ui_goto_field_t active;
    char lat_buf[20];
    char lon_buf[20];
    bool lat_north;
    bool lon_east;
} ui_goto_t;

ui_goto_t *ui_goto_create(lv_event_cb_t tab_cb);

void ui_goto_set_format(ui_goto_t *g, ui_coord_fmt_t fmt);
void ui_goto_set_field(ui_goto_t *g, ui_goto_field_t field);
void ui_goto_set_coords(ui_goto_t *g, const char *lat, const char *lon);
void ui_goto_set_recent(ui_goto_t *g, const char *name, float dist_mi, int brg);

/* Raw entry text for the active field, e.g. "32 59.6420". */
const char *ui_goto_get_lat(ui_goto_t *g);
const char *ui_goto_get_lon(ui_goto_t *g);

/* Fires when "Start Navigation" is tapped. */
void ui_goto_set_start_cb(ui_goto_t *g, lv_event_cb_t cb, void *user_data);
void ui_goto_set_cancel_cb(ui_goto_t *g, lv_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
#endif /* UI_GOTO_H */
