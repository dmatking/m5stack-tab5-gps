/*
 * ui_driving.h — full-screen "driving mode": one giant speed readout,
 * no other chrome. Direct feedback from the first real multi-hour field
 * test: Home's SPEED card is too small to read at a glance while driving.
 */
#ifndef UI_DRIVING_H
#define UI_DRIVING_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *speed;       /* giant number, or "--" with no fix/no speed yet */
    lv_obj_t *speed_unit;  /* "mph"/"km/h" */
} ui_driving_t;

/* Builds the screen but does not load it. */
ui_driving_t *ui_driving_create(void);

// valid == false (no fix, or speed not reported yet this session) shows
// "--" in a muted color instead of `speed` -- same "no data yet"
// convention as every other real number in this app, see
// ui_home_set_speed()'s own comment (main/ui_home.h) for why this matters:
// a giant confident-looking number is exactly the wrong thing to show
// with no real data behind it.
void ui_driving_set_speed(ui_driving_t *d, float speed, const char *unit, bool valid);

// Tap anywhere on the screen returns to Home -- registered on the screen
// root itself, no button/chrome to keep this screen as uncluttered as its
// whole purpose requires.
void ui_driving_set_exit_cb(ui_driving_t *d, lv_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
#endif /* UI_DRIVING_H */
