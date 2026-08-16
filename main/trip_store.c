// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "trip_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "TRIP_STORE";

#define NVS_NAMESPACE "trip"
#define KEY_TOTALS    "totals"   // one blob, not four scalar keys -- NVS has
                                  // no native float get/set, and a blob sidesteps
                                  // bit-casting every field to store one anyway.

void trip_store_load(trip_totals_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    nvs_handle_t h;
    // Relies on the default NVS partition already being initialized --
    // app_settings_init() does that (nvs_flash_init()) well before
    // gps_ui_bridge_start() calls this, same assumption app_settings.c
    // itself makes about its own nvs_open() calls.
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no stored trip yet, starting at zero");
        return;
    }

    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, KEY_TOTALS, out, &len);
    nvs_close(h);

    if (err != ESP_OK || len != sizeof(*out)) {
        memset(out, 0, sizeof(*out));  // wrong size or missing -- don't half-trust it
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "stored trip blob unusable (%s) -- starting at zero", esp_err_to_name(err));
        }
        return;
    }

    ESP_LOGI(TAG, "loaded trip: %.2f mi, %.1f mph max, %u s moving, %.0f ft gain",
             out->distance_mi, out->max_mph, (unsigned)out->moving_s, out->elev_gain_ft);
}

void trip_store_save(const trip_totals_t *t)
{
    if (!t) return;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        // Same fallback as app_settings.c's write path -- not load-bearing
        // enough to justify anything more than logging it away.
        ESP_LOGW(TAG, "nvs_open (write) failed: %s -- trip not persisted this round", esp_err_to_name(err));
        return;
    }
    nvs_set_blob(h, KEY_TOTALS, t, sizeof(*t));
    nvs_commit(h);
    nvs_close(h);
}

void trip_store_clear(void)
{
    trip_totals_t zero = { 0 };
    trip_store_save(&zero);
}
