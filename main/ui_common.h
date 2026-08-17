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
    lv_obj_t *clock;    /* "10:24 AM"           */
    lv_obj_t *batt;     /* "87%"                */
} ui_status_t;

/* Single-row status header, identical on every screen: fix dot + fix text +
 * sat count on the left, clock + battery on the right.
 *
 * Used to take a `compact` flag choosing between this and a taller two-row
 * variant (second row: sat count, HDOP, a battery glyph). That variant is
 * gone -- its second row never actually rendered on Home, the only screen
 * that used it, and the one-row version never rendered *at all* on Map /
 * Nav / Telemetry / Goto. Both failures traced to the same thing: the row
 * was a nested LV_SIZE_CONTENT flex container inside this root, and its
 * content landed ~54px below where the layout math put it, so it fell
 * outside the root's own clip box (fully at 66px, second-row-only at
 * 109px). Building the row directly in root -- no intermediate container --
 * is the same flattening that fixed Settings' hand-rolled header, which had
 * a nested SIZE_CONTENT row of its own and garbled its clock the moment it
 * started updating. See ui_settings.c's header for that precedent. */
void ui_status_create(lv_obj_t *parent, ui_status_t *out);

void ui_status_set_fix(ui_status_t *s, const char *fix_text, bool good);
// HDOP dropped along with the two-row variant (see ui_status_create()) --
// it's still shown where it's actually useful: Telemetry's own HDOP card,
// and Home's GPS ACCURACY card, which derives feet/meters from it.
void ui_status_set_sats(ui_status_t *s, int in_solution);
void ui_status_set_clock(ui_status_t *s, const char *clock_text);
void ui_status_set_battery(ui_status_t *s, int percent);
// No battery installed, running on external (USB) power -- see
// main/battery.h's battery_read(). Swaps the "NN%" text for a charge-plug
// glyph in a neutral color (neither the low-battery red nor the normal
// green -- it's not a warning, just a different, valid state).
void ui_status_set_battery_external(ui_status_t *s);

/* Bottom tab bar. `cb` is called with the ui_tab_t as user data when a tab is
 * tapped; pass NULL to make it non-interactive. */
lv_obj_t *ui_navbar_create(lv_obj_t *parent, ui_tab_t active, lv_event_cb_t cb);

#ifdef __cplusplus
}
#endif
#endif /* UI_COMMON_H */
