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
    lv_obj_t *tab_entry_label;
    lv_obj_t *tab_saved_label;

    // The segmented control shows/hides these two groups of widgets
    // directly -- deliberately NOT wrapped in an intermediate pane
    // container. That was the first attempt, and it silently broke: an
    // extra flex_grow(1)-inside-flex_col level between `body` and its
    // existing children shifted the LAST couple of them (the keypad's
    // bottom rows, and the recent-waypoint card entirely) off whatever
    // LVGL's layout pass actually reported, the same rendering failure
    // that hid the shared status bar and Settings' header earlier this
    // project. Every child below is still a direct, flat child of `body`,
    // exactly like before this feature existed -- only individual HIDDEN
    // flags change.
    bool      saved_mode;
    bool      has_recent;    /* whether the recent-waypoint card has anything to show */

    lv_obj_t *fmts;          /* coordinate-format button row */
    lv_obj_t *pad;           /* keypad */
    lv_obj_t *rec;           /* recent-waypoint card */

    lv_obj_t *saved_list;    /* scrollable container of waypoint rows */
    lv_obj_t *saved_empty;   /* "No saved waypoints yet" placeholder label */

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
    lv_obj_t *btn_start_wrap;  /* hidden on the saved pane -- see ui_goto_set_tab() */

    ui_goto_field_t active;
    char lat_buf[20];
    char lon_buf[20];
    bool lat_north;
    bool lon_east;
} ui_goto_t;

ui_goto_t *ui_goto_create(lv_event_cb_t tab_cb);

// Switch the segmented control between coordinate entry and the saved list.
// Also called internally when either tab is tapped.
void ui_goto_set_tab(ui_goto_t *g, bool saved);

// Rebuild the saved-waypoint list. Streaming rather than taking an array,
// so this header stays ignorant of the waypoint store (design_ui.c owns the
// loop, the same way it owns the start/cancel/format wiring).
//
// IMPORTANT: the row callbacks carry a list index, so ANY mutation of the
// store must be followed by a full rebuild -- otherwise a tap after a
// delete navigates to the wrong waypoint.
void ui_goto_saved_begin(ui_goto_t *g);
void ui_goto_saved_add(ui_goto_t *g, int index, const char *name, const char *meta);
void ui_goto_saved_end(ui_goto_t *g);

// go_cb fires when a row is tapped, del_cb when its trash button is.
void ui_goto_set_saved_cbs(ui_goto_t *g, void (*go_cb)(int), void (*del_cb)(int));

void ui_goto_set_format(ui_goto_t *g, ui_coord_fmt_t fmt);
void ui_goto_set_field(ui_goto_t *g, ui_goto_field_t field);
void ui_goto_set_coords(ui_goto_t *g, const char *lat, const char *lon);

// Recent-waypoint card on the entry pane. name == NULL hides the card
// entirely (nothing saved yet). meta is shown as-is below the name --
// design_ui.c currently passes plain coordinates; there's no live
// distance/bearing here the way the old demo text implied ("last used |
// 6.4 mi | 094°"), since that needs a position tick this card doesn't get.
void ui_goto_set_recent(ui_goto_t *g, const char *name, const char *meta);

/* Raw entry text for the active field, e.g. "32 59.6420". */
const char *ui_goto_get_lat(ui_goto_t *g);
const char *ui_goto_get_lon(ui_goto_t *g);

// Interprets lat_buf/lon_buf as decimal degrees per the currently selected
// format (g->fmt) and hemisphere flags -- see ui_goto.c's parse_field() for
// the digit-width convention each format uses (a new one; there was no
// pre-existing parser to match, the format toggle only ever changed which
// button was highlighted before this). Out-of-range input is clamped, not
// rejected; returns false (leaves *out_lat/*out_lon untouched) for a NULL
// g/out pointer, or when both fields are still empty.
bool ui_goto_parse(ui_goto_t *g, double *out_lat, double *out_lon);

/* Fires when "Start Navigation" is tapped. */
void ui_goto_set_start_cb(ui_goto_t *g, lv_event_cb_t cb, void *user_data);
void ui_goto_set_cancel_cb(ui_goto_t *g, lv_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
#endif /* UI_GOTO_H */
