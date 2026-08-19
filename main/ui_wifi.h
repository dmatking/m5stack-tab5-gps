/*
 * ui_wifi.h — Wi-Fi credential entry screen, reached from Settings'
 * CONNECTIVITY > Wi-Fi row (main/ui_settings.c's UI_SET_WIFI row).
 *
 * Widget creation/layout only, same ownership split as every other screen
 * in this app -- main/wifi_ui_bridge.c owns the actual esp_wifi/wifi_prov
 * calls and pushes status back in via ui_wifi_set_status(), design_ui.c
 * wires the Cancel/Connect button callbacks to it.
 */
#ifndef UI_WIFI_H
#define UI_WIFI_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_WIFI_DISCONNECTED = 0,
    UI_WIFI_CONNECTING,
    UI_WIFI_CONNECTED,
    UI_WIFI_FAILED,
} ui_wifi_status_t;

typedef struct {
    lv_obj_t *screen;

    lv_obj_t *status_dot;
    lv_obj_t *status_label;

    lv_obj_t *ssid_ta;
    lv_obj_t *pass_ta;
    lv_obj_t *keyboard;    /* hidden until an lv_textarea gets focus */

    lv_obj_t *btn_cancel;
    lv_obj_t *btn_connect;
} ui_wifi_t;

ui_wifi_t *ui_wifi_create(lv_event_cb_t tab_cb);

// Updates the dot + status line at the top of the screen. `ssid` is shown
// for CONNECTING/CONNECTED (ignored otherwise); NULL is fine for any state.
void ui_wifi_set_status(ui_wifi_t *w, ui_wifi_status_t status, const char *ssid);

// Raw typed text -- design_ui.c reads these when Connect is tapped, same
// ownership split as ui_goto.c's ui_goto_get_lat()/get_lon().
const char *ui_wifi_get_ssid(ui_wifi_t *w);
const char *ui_wifi_get_password(ui_wifi_t *w);

// Pre-fills the SSID field (e.g. with the last-stored SSID on screen
// entry) -- password is deliberately never pre-filled or read back, same
// as wifi_prov's own portal form.
void ui_wifi_set_ssid(ui_wifi_t *w, const char *ssid);

void ui_wifi_set_connect_cb(ui_wifi_t *w, lv_event_cb_t cb, void *user_data);
void ui_wifi_set_cancel_cb(ui_wifi_t *w, lv_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif
#endif /* UI_WIFI_H */
