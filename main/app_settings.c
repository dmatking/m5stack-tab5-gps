// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "app_settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "APP_SETTINGS";
#define NVS_NAMESPACE      "app_settings"
#define KEY_TIME_24H       "time_24h"
#define KEY_BRIGHTNESS     "brightness"
#define KEY_KEEP_SCREEN_ON "keep_scr_on"
#define KEY_COORD_FORMAT   "coord_fmt"

static bool s_time_24h; // default false (12-hour) -- matches the design's own "10:24 AM" demo strings
static int  s_brightness = 72;      // default matches ui_settings.c's own demo value
static bool s_keep_screen_on = true; // default matches ui_settings_create()'s creation-time switch state
static int  s_coord_format;          // default 0 = DD MM.MMMM, matches ui_goto.h's UI_COORD_DDM/ui_settings.c's demo value

void app_settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Standard ESP-IDF pattern: a corrupted/format-mismatched NVS
        // partition (e.g. after a partition-table change) needs one erase
        // before init can succeed -- not an error worth failing boot over.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    nvs_handle_t h;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // First boot ever (namespace doesn't exist yet) or some other open
        // failure -- fall back to defaults rather than fail boot over a
        // settings store that's allowed to be empty.
        ESP_LOGI(TAG, "no stored settings yet (%s), using defaults", esp_err_to_name(err));
        return;
    }

    uint8_t v = 0;
    if (nvs_get_u8(h, KEY_TIME_24H, &v) == ESP_OK) {
        s_time_24h = v != 0;
    }
    if (nvs_get_u8(h, KEY_KEEP_SCREEN_ON, &v) == ESP_OK) {
        s_keep_screen_on = v != 0;
    }
    int32_t br = 0;
    if (nvs_get_i32(h, KEY_BRIGHTNESS, &br) == ESP_OK) {
        s_brightness = (int)br;
    }
    int32_t cf = 0;
    if (nvs_get_i32(h, KEY_COORD_FORMAT, &cf) == ESP_OK) {
        s_coord_format = (int)cf;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: time_24h=%d brightness=%d keep_screen_on=%d coord_format=%d",
             s_time_24h, s_brightness, s_keep_screen_on, s_coord_format);
}

bool app_settings_get_time_24h(void)
{
    return s_time_24h;
}

void app_settings_set_time_24h(bool on)
{
    s_time_24h = on;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open (write) failed: %s -- setting applied this session only", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, KEY_TIME_24H, on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

int app_settings_get_brightness(void)
{
    return s_brightness;
}

void app_settings_set_brightness(int percent)
{
    if (percent < 5)   percent = 5;
    if (percent > 100) percent = 100;
    s_brightness = percent;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open (write) failed: %s -- setting applied this session only", esp_err_to_name(err));
        return;
    }
    nvs_set_i32(h, KEY_BRIGHTNESS, s_brightness);
    nvs_commit(h);
    nvs_close(h);
}

bool app_settings_get_keep_screen_on(void)
{
    return s_keep_screen_on;
}

void app_settings_set_keep_screen_on(bool on)
{
    s_keep_screen_on = on;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open (write) failed: %s -- setting applied this session only", esp_err_to_name(err));
        return;
    }
    nvs_set_u8(h, KEY_KEEP_SCREEN_ON, on ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

int app_settings_get_coord_format(void)
{
    return s_coord_format;
}

void app_settings_set_coord_format(int fmt)
{
    if (fmt < 0 || fmt > 2) return; // only 3 valid formats, see this header's own comment
    s_coord_format = fmt;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open (write) failed: %s -- setting applied this session only", esp_err_to_name(err));
        return;
    }
    nvs_set_i32(h, KEY_COORD_FORMAT, s_coord_format);
    nvs_commit(h);
    nvs_close(h);
}
