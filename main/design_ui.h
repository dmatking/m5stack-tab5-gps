/*
 * design_ui.h (originally ui.h in the hand-off zip, renamed to avoid
 * colliding with this project's own generic-sounding filenames) —
 * entry point for the generated design UI. Call ui_init() once after
 * your LVGL display is up.
 */
#ifndef DESIGN_UI_H
#define DESIGN_UI_H

#include "ui_home.h"
#include "ui_map.h"
#include "ui_nav.h"
#include "ui_telemetry.h"
#include "ui_goto.h"
#include "ui_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);

/* Tab routing. UI_TAB_NAV shows the active-navigation screen while a
 * destination is set, and the Goto entry screen otherwise. UI_TAB_MAP is
 * intercepted in design_ui.c -- see its header comment -- and hands off
 * to the native map renderer instead of loading ui_map's own screen. */
void ui_show_tab(ui_tab_t tab);
void ui_show_goto(void);
void ui_show_nav(void);

/* Flips what UI_TAB_NAV resolves to and reveals the map's route overlay. */
void ui_set_navigating(bool navigating);
bool ui_is_navigating(void);

ui_home_t      *ui_home(void);
ui_map_t       *ui_map(void);
ui_nav_t       *ui_nav(void);
ui_telemetry_t *ui_telemetry(void);
ui_goto_t      *ui_goto(void);
ui_settings_t  *ui_settings(void);

#ifdef __cplusplus
}
#endif
#endif /* DESIGN_UI_H */
