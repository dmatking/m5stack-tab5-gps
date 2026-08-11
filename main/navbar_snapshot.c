// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "navbar_snapshot.h"

#include <string.h>

#include "ui_common.h"
#include "ui_theme.h"

#include "lvgl.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "NAVBAR_SNAPSHOT";
static uint16_t *s_buf; // UI_SCREEN_W x UI_NAVBAR_H, PSRAM, owned by this file

void navbar_snapshot_capture(void)
{
    // Throwaway, never-attached-to-a-screen parent sized to exactly match
    // the real navbar's own LV_PCT(100) x UI_NAVBAR_H footprint -- ui_common.c's
    // ui_navbar_create() fills whatever parent it's given.
    lv_obj_t *tmp = lv_obj_create(NULL);
    lv_obj_remove_style_all(tmp);
    lv_obj_set_size(tmp, UI_SCREEN_W, UI_NAVBAR_H);

    ui_navbar_create(tmp, UI_TAB_MAP, NULL);
    lv_obj_update_layout(tmp); // flex layout needs at least one pass before it's ready to render

    lv_draw_buf_t *snap = lv_snapshot_take(tmp, LV_COLOR_FORMAT_RGB565);
    if (!snap) {
        ESP_LOGE(TAG, "lv_snapshot_take failed");
        lv_obj_delete(tmp);
        return;
    }

    if (!s_buf) {
        s_buf = heap_caps_malloc((size_t)UI_SCREEN_W * UI_NAVBAR_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_buf) {
        ESP_LOGE(TAG, "PSRAM alloc failed");
        lv_draw_buf_destroy(snap);
        lv_obj_delete(tmp);
        return;
    }

    // Copy row-by-row using the snapshot's own stride -- lv_snapshot_take()
    // doesn't guarantee stride == w*2 (alignment padding is allowed), but
    // navbar_snapshot_get()'s contract promises a tightly-packed buffer, so
    // this is where that gets made true, not assumed.
    for (int y = 0; y < UI_NAVBAR_H; y++) {
        const uint8_t *src_row = snap->data + (size_t)y * snap->header.stride;
        memcpy(s_buf + (size_t)y * UI_SCREEN_W, src_row, (size_t)UI_SCREEN_W * sizeof(uint16_t));
    }

    lv_draw_buf_destroy(snap);
    lv_obj_delete(tmp);
    ESP_LOGI(TAG, "captured %dx%d navbar snapshot", UI_SCREEN_W, UI_NAVBAR_H);
}

const uint16_t *navbar_snapshot_get(void)
{
    return s_buf;
}
