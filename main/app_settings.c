// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "app_settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "APP_SETTINGS";
#define NVS_NAMESPACE "app_settings"
#define KEY_TIME_24H  "time_24h"

static bool s_time_24h; // default false (12-hour) -- matches the design's own "10:24 AM" demo strings

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
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: time_24h=%d", s_time_24h);
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
