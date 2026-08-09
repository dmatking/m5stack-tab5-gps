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

#ifdef __cplusplus
extern "C" {
#endif

void ui_init(void);
void ui_show_tab(ui_tab_t tab);

ui_home_t *ui_home(void);
ui_map_t  *ui_map(void);

#ifdef __cplusplus
}
#endif
#endif /* DESIGN_UI_H */
