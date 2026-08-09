/*
 * ui_settings.h — 2A More / Settings screen
 */
#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SET_NIGHT_MODE = 0,
    UI_SET_UNITS,
    UI_SET_ELEVATION,
    UI_SET_COORD_FORMAT,
    UI_SET_TIMEZONE,
    UI_SET_CONSTELLATIONS,
    UI_SET_UPDATE_RATE,
    UI_SET_TRACK_LOG,
    UI_SET_COUNT
} ui_setting_id_t;

typedef struct {
    lv_obj_t *screen;
    ui_status_t status;

    lv_obj_t *brightness;                 /* lv_slider              */
    lv_obj_t *brightness_pct;
    lv_obj_t *value[UI_SET_COUNT];        /* right-hand value label */
    lv_obj_t *sw_screen_on;
    lv_obj_t *sw_sbas;
    lv_obj_t *sd_usage;
    lv_obj_t *footer;
} ui_settings_t;

ui_settings_t *ui_settings_create(lv_event_cb_t tab_cb);

void ui_settings_set_brightness(ui_settings_t *s, int percent);
void ui_settings_set_value(ui_settings_t *s, ui_setting_id_t id, const char *text);
void ui_settings_set_screen_on(ui_settings_t *s, bool on);
void ui_settings_set_sbas(ui_settings_t *s, bool on);
void ui_settings_set_storage(ui_settings_t *s, float used_gb, float total_gb);
void ui_settings_set_footer(ui_settings_t *s, const char *line1, const char *line2);

/* Row taps report their ui_setting_id_t as event user data. */
void ui_settings_set_row_cb(ui_settings_t *s, lv_event_cb_t cb);
/* Brightness slider LV_EVENT_VALUE_CHANGED. */
void ui_settings_set_brightness_cb(ui_settings_t *s, lv_event_cb_t cb);

#ifdef __cplusplus
}
#endif
#endif /* UI_SETTINGS_H */
