// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "board_interface.h"
#include "map_view.h"
#include "sd_card.h"
#include "tile_cache.h"
#include "tile_flash.h"
#include "touch.h"
#include "ui_overlay.h"

static const char *TAG = "APP";

// Temporary diagnostic: list whatever's on the SD card's root directory, to
// prove the FAT filesystem layer actually works (not just that SDMMC
// bring-up succeeded). Superseded once real tile-reading code (tile_sd.c)
// exists -- see the "SD card read verification" plan.
static void sd_card_list_root(void)
{
    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        ESP_LOGW(TAG, "opendir(%s) failed", SD_MOUNT_POINT);
        return;
    }

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL) {
        char path[300];
        snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            ESP_LOGI(TAG, "  %s  %s  %ld bytes",
                     ent->d_name, S_ISDIR(st.st_mode) ? "<DIR>" : "     ", (long)st.st_size);
        } else {
            ESP_LOGI(TAG, "  %s  (stat failed)", ent->d_name);
        }
        count++;
    }
    closedir(dir);
    ESP_LOGI(TAG, "SD card root: %d entr%s", count, count == 1 ? "y" : "ies");
}

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

    if (sd_card_mount()) {
        sd_card_list_root();
    }

    tile_flash_init();
    tile_cache_init();
    ui_overlay_init();
    map_view_start();
}
