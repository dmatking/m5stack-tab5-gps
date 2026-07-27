// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "esp_log.h"

#include "board_interface.h"
#include "map_view.h"
#include "tile_cache.h"
#include "tile_flash.h"
#include "touch.h"
#include "ui_overlay.h"

static const char *TAG = "APP";

void app_main(void)
{
    ESP_LOGI(TAG, "app_main starting");
    board_init();

    if (!board_has_lcd()) {
        ESP_LOGE(TAG, "Board has no LCD configured -- nothing to do.");
        return;
    }

    if (!touch_init()) {
        ESP_LOGW(TAG, "Touch unavailable -- map will render but won't be draggable.");
    }

    tile_flash_init();
    tile_cache_init();
    ui_overlay_init();
    map_view_start();
}
