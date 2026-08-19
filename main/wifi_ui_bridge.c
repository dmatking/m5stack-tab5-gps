// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Owns the actual esp_wifi calls behind Settings' Wi-Fi row and the Wi-Fi
// screen (main/ui_wifi.c) -- same translation-layer role gps_ui_bridge.c
// plays for GPS data. Deliberately does NOT call wifi_prov_start() for
// either the boot-time auto-reconnect or the on-device Connect button:
// that function blocks until connected and, on failure, falls into its own
// captive-portal AP + web server loop that never returns -- fine for a
// device's very first out-of-box setup, but wrong here twice over: it
// would silently start a stray SoftAP whenever a stored/typed password is
// wrong, and this project's on-device Settings screen is specifically
// meant to replace that portal, not sit in front of it. Both paths below
// go through connect_task() instead, a direct STA connect (mirrors
// wifi_prov.c's own sta_connect(), minus the portal fallback) that always
// reports back -- UI_WIFI_CONNECTED or UI_WIFI_FAILED -- through the same
// two UI surfaces. Credentials still live in wifi_prov's own NVS
// namespace/keys throughout, so nothing about its storage format changes.

#include "wifi_ui_bridge.h"

#include <string.h>

#include "design_ui.h"
#include "wifi_prov.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WIFI_UI";

#define CONNECT_TIMEOUT_MS 20000

typedef enum { ST_DISCONNECTED, ST_CONNECTING, ST_CONNECTED, ST_FAILED } wifi_state_t;

// s_state/s_ssid are only ever touched while lvgl_port_lock() is held --
// piggybacking on that lock (already required to touch the lv_obj widgets
// these feed) instead of adding a second mutex for two small fields shared
// between the event-handler context (esp_event's system task) and
// connect_task()'s own background task.
static wifi_state_t s_state = ST_DISCONNECTED;
static char         s_ssid[33];

static bool s_wifi_stack_up;   // esp_wifi_init() done at least once

// Set by wifi_ui_bridge_connect() just before spawning connect_task();
// only one connect attempt is ever expected in flight at a time (a human
// tapping Connect, or the one boot-time auto-reconnect), so a pair of
// static buffers is enough -- no queue.
static char s_pending_ssid[33];
static char s_pending_pass[65];

// Must be called with lvgl_port_lock() already held. Mirrors s_state into
// both UI surfaces that show it -- the Wi-Fi screen's own status line
// (only meaningful while that screen exists, i.e. always, since design_ui.c
// creates it once at boot like every other screen) and Settings' summary
// row.
static void push_ui(void)
{
    ui_wifi_status_t ui_status;
    const char *settings_text;
    switch (s_state) {
    case ST_CONNECTING:
        ui_status = UI_WIFI_CONNECTING;
        settings_text = "Connecting...";
        break;
    case ST_CONNECTED:
        ui_status = UI_WIFI_CONNECTED;
        settings_text = s_ssid[0] ? s_ssid : "Connected";
        break;
    case ST_FAILED:
        ui_status = UI_WIFI_FAILED;
        settings_text = "Connection failed";
        break;
    case ST_DISCONNECTED:
    default:
        ui_status = UI_WIFI_DISCONNECTED;
        settings_text = "Not connected";
        break;
    }

    ui_wifi_t *wui = ui_wifi();
    if (wui) ui_wifi_set_status(wui, ui_status, s_ssid);

    ui_settings_t *set = ui_settings();
    if (set) ui_settings_set_value(set, UI_SET_WIFI, settings_text);
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    LV_UNUSED(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // While a connect attempt is still in flight, one or more of these
        // is a normal part of associating -- connect_task()'s own timeout
        // loop is what decides CONNECTING -> FAILED, not this handler.
        // Once actually connected, a drop always gets one immediate
        // reconnect attempt (no backoff/retry-limit yet -- this project's
        // primary use case is USB power in a car, so a WiFi drop is more
        // likely "briefly out of range" than "gone for good"; see
        // m5stack-tab5-ssh-terminal's own exponential-backoff pattern if
        // this ever needs to be gentler about it).
        if (lvgl_port_lock(50)) {
            if (s_state == ST_CONNECTED) {
                s_state = ST_DISCONNECTED;
                push_ui();
                esp_wifi_connect();
            }
            lvgl_port_unlock();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (lvgl_port_lock(50)) {
            s_state = ST_CONNECTED;
            wifi_ap_record_t info;
            if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) {
                strncpy(s_ssid, (const char *)info.ssid, sizeof(s_ssid) - 1);
                s_ssid[sizeof(s_ssid) - 1] = '\0';
            }
            push_ui();
            lvgl_port_unlock();
        }
    }
}

// Direct STA connect -- see this file's header comment for why this
// exists instead of calling wifi_prov_start(). Runs in its own task since
// it waits (with a bounded timeout) for the connection outcome; a plain
// button-tap callback runs on the LVGL task and would freeze the whole UI
// for as long as that takes.
static void connect_task(void *arg)
{
    LV_UNUSED(arg);
    char ssid[33], pass[65];
    strncpy(ssid, s_pending_ssid, sizeof(ssid) - 1); ssid[sizeof(ssid) - 1] = '\0';
    strncpy(pass, s_pending_pass, sizeof(pass) - 1); pass[sizeof(pass) - 1] = '\0';

    if (lvgl_port_lock(50)) {
        s_state = ST_CONNECTING;
        strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
        s_ssid[sizeof(s_ssid) - 1] = '\0';
        push_ui();
        lvgl_port_unlock();
    }

    wifi_config_t wcfg = { 0 };
    strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, pass, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    if (!s_wifi_stack_up) {
        esp_netif_init();                     // ESP_ERR_INVALID_STATE if
        esp_event_loop_create_default();      // already up -- ignored,
                                               // same as wifi_prov_start()'s
                                               // own "ignore already-
                                               // initialized errors".
        esp_netif_create_default_wifi_sta();
        wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t err = esp_wifi_init(&init_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        }
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_handler, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            wifi_event_handler, NULL, NULL);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(WIFI_IF_STA, &wcfg);
        err = esp_wifi_start();   // fires WIFI_EVENT_STA_START -> handler connects
        if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
        s_wifi_stack_up = true;
    } else {
        // Already up from an earlier attempt -- drop whatever it's doing
        // and reconfigure with the newly typed credentials directly,
        // rather than tearing the whole stack down and back up.
        esp_wifi_disconnect();
        esp_wifi_set_config(WIFI_IF_STA, &wcfg);
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);

    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(CONNECT_TIMEOUT_MS);
    while (xTaskGetTickCount() < deadline) {
        bool still_connecting = true;
        if (lvgl_port_lock(50)) {
            still_connecting = (s_state == ST_CONNECTING);
            lvgl_port_unlock();
        }
        if (!still_connecting) break;
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (lvgl_port_lock(50)) {
        if (s_state == ST_CONNECTING) {
            ESP_LOGW(TAG, "Timed out connecting to '%s'", ssid);
            s_state = ST_FAILED;
            push_ui();
        }
        lvgl_port_unlock();
    }

    vTaskDelete(NULL);
}

void wifi_ui_bridge_connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) return;

    // Persist into wifi_prov's own NVS store first, so a future boot's
    // wifi_ui_bridge_init() picks these up automatically -- same
    // namespace/keys its own portal flow would have written, just from
    // this screen instead of a captive-portal web form.
    nvs_handle_t nvs;
    if (nvs_open("wifi_prov", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "ssid", ssid);
        nvs_set_str(nvs, "pass", pass ? pass : "");
        nvs_commit(nvs);
        nvs_close(nvs);
    } else {
        ESP_LOGW(TAG, "Failed to open 'wifi_prov' NVS namespace -- credentials not persisted");
    }

    strncpy(s_pending_ssid, ssid, sizeof(s_pending_ssid) - 1);
    s_pending_ssid[sizeof(s_pending_ssid) - 1] = '\0';
    strncpy(s_pending_pass, pass ? pass : "", sizeof(s_pending_pass) - 1);
    s_pending_pass[sizeof(s_pending_pass) - 1] = '\0';

    xTaskCreate(connect_task, "wifi_connect", 4096, NULL, 3, NULL);
}

void wifi_ui_bridge_init(void)
{
    char ssid[33] = { 0 };
    char pass[65] = { 0 };
    bool have_ssid = wifi_prov_get("ssid", ssid, sizeof(ssid));
    wifi_prov_get("pass", pass, sizeof(pass));   // absent is fine -- open network

    ui_wifi_t *wui = ui_wifi();
    if (wui && lvgl_port_lock(50)) {
        ui_wifi_set_ssid(wui, ssid);   // pre-fill with the last-stored SSID
        lvgl_port_unlock();
    }

    if (have_ssid && ssid[0]) {
        ESP_LOGI(TAG, "Stored SSID found ('%s'), reconnecting...", ssid);
        wifi_ui_bridge_connect(ssid, pass);
    }
}
