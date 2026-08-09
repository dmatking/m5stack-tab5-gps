/*
 * ui_common.h — status bar and bottom tab bar shared by every screen
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_TAB_HOME = 0,
    UI_TAB_MAP,
    UI_TAB_NAV,
    UI_TAB_TELEMETRY,
    UI_TAB_MORE,
    UI_TAB_COUNT
} ui_tab_t;

/* Handles kept so live values can be pushed in without rebuilding. */
typedef struct {
    lv_obj_t *root;
    lv_obj_t *fix;      /* "GPS FIX"            */
    lv_obj_t *dot;      /* fix indicator dot    */
    lv_obj_t *sats;     /* "14 sats"            */
    lv_obj_t *hdop;     /* "HDOP 0.8"           */
    lv_obj_t *clock;    /* "10:24 AM"           */
    lv_obj_t *batt;     /* "87%"                */
    lv_obj_t *batt_fill;
} ui_status_t;

/* Two-row status header (as on 2A Home). Pass compact = true for the
 * single-row variant used on Map / Nav / Telemetry / Goto. */
void ui_status_create(lv_obj_t *parent, ui_status_t *out, bool compact);

void ui_status_set_fix(ui_status_t *s, const char *fix_text, bool good);
void ui_status_set_sats(ui_status_t *s, int in_solution, float hdop);
void ui_status_set_clock(ui_status_t *s, const char *clock_text);
void ui_status_set_battery(ui_status_t *s, int percent);

/* Bottom tab bar. `cb` is called with the ui_tab_t as user data when a tab is
 * tapped; pass NULL to make it non-interactive. */
lv_obj_t *ui_navbar_create(lv_obj_t *parent, ui_tab_t active, lv_event_cb_t cb);

#ifdef __cplusplus
}
#endif
#endif /* UI_COMMON_H */
