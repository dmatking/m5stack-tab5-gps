// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"

#include "board_interface.h"
#include "gps.h"
#include "gps_ui_bridge.h"
#include "map_view.h"
#include "sd_card.h"
#include "tile_cache.h"
#include "tile_jpeg.h"
#include "tile_sd.h"
#include "sd_xfer.h"
#include "touch.h"
#include "ui_overlay.h"
#include "ui_shell.h"
#include "usb_msc.h"

static const char *TAG = "APP";

// Diagnostic: list whatever's on the SD card's root directory -- useful for
// confirming tile_sd.c's shard-file naming convention landed correctly,
// alongside gps_log.txt/tiles.bin/etc.
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

#ifdef APP_USB_MSC_MODE
    // One-off dev-tool build: expose the SD card as a USB drive and stop --
    // see main/usb_msc.c and the "USB mass-storage mode" plan. Doesn't touch
    // the LCD/touch/tile stack at all; to go back to the normal map app,
    // just rebuild/reflash with USB_MSC_MODE unset.
    if (!usb_msc_start()) {
        ESP_LOGE(TAG, "USB mass storage init failed -- nothing else to do in this build.");
    }
    return;
#elif defined(APP_SD_XFER_MODE)
    // One-off dev-tool build: receive a file over the console (COM17) and
    // write it to the SD card -- see main/sd_xfer.c. Requires the
    // sdkconfig.sdxfer.defaults overlay (COM17 as primary console); doesn't
    // touch the LCD/touch/tile stack at all. sd_xfer_run() never returns.
    sd_xfer_run();
    return;
#else
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

    gps_init();  // opens a log file on /sdcard if mounted -- must come after sd_card_mount()

    tile_jpeg_init();  // shared decoder -- must come before tile_sd_init()
    tile_sd_init();  // scans /sdcard for shard files -- must come after sd_card_mount()
    tile_cache_init();
    ui_overlay_init();

    // Everything above has to be ready before this: the shell's Map button
    // hands off straight into map_view_start(), see ui_shell.c.
    ui_shell_start();

    // ui_shell_start() builds the Home screen synchronously (under lock)
    // before returning, so ui_home() is already valid here even though the
    // splash screen is still showing in front of it.
    gps_ui_bridge_start();
#endif
}
