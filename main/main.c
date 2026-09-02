// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include <dirent.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_hosted.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_settings.h"
#include "battery.h"
#include "board_interface.h"
#include "fb_capture.h"
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
#include "waypoints.h"
#include "wifi_ui_bridge.h"

static const char *TAG = "APP";

// esp_hosted_connect_to_slave() does the actual SDIO handshake with the
// physical C6 chip -- confirmed flaky on a true cold boot on real hardware
// (battery rail still stabilizing right after power-on, presumably): hit 3
// back-to-back failures before it finally succeeded. ESP_ERROR_CHECK on a
// transient failure here means the WHOLE DEVICE hard-aborts and reboots --
// GPS/map/nav included -- just because WiFi's handshake needs another
// attempt, wildly disproportionate since nothing else in this app depends
// on WiFi being up. Retries a few times (full deinit+reinit each attempt,
// not assuming partial state is safely resumable) with a short settle
// delay; if it never comes up, the caller just boots without WiFi instead
// of refusing to start at all.
#define ESP_HOSTED_CONNECT_MAX_ATTEMPTS 3
static bool hosted_bringup(void)
{
    for (int attempt = 1; attempt <= ESP_HOSTED_CONNECT_MAX_ATTEMPTS; attempt++) {
        esp_err_t err = esp_hosted_init();
        if (err == ESP_OK) err = esp_hosted_connect_to_slave();
        if (err == ESP_OK) return true;

        ESP_LOGW(TAG, "esp_hosted bring-up attempt %d/%d failed: %s",
                 attempt, ESP_HOSTED_CONNECT_MAX_ATTEMPTS, esp_err_to_name(err));
        esp_hosted_deinit();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

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

    // Bring up the P4<->C6 SDIO link (see sdkconfig.defaults' WiFi block
    // for the Tab5-specific pin config). Must run before nvs_flash_init()/
    // esp_wifi_init()/etc -- app_settings_init() right below does the
    // former -- matching every sibling project's proven init order.
    bool wifi_hw_ready = hosted_bringup();
    if (wifi_hw_ready) {
        esp_hosted_coprocessor_fwver_t fwver = {0};
        if (esp_hosted_get_coprocessor_fwversion(&fwver) == ESP_OK) {
            ESP_LOGI(TAG, "C6 co-processor connected, firmware v%lu.%lu.%lu",
                     (unsigned long)fwver.major1, (unsigned long)fwver.minor1,
                     (unsigned long)fwver.patch1);
        } else {
            ESP_LOGW(TAG, "C6 co-processor connected, but firmware version query failed");
        }
    } else {
        ESP_LOGE(TAG, "esp_hosted bring-up failed after %d attempts -- continuing without WiFi",
                 ESP_HOSTED_CONNECT_MAX_ATTEMPTS);
    }

    app_settings_init(); // before ui_shell_start() -- the Settings screen needs real persisted values at creation time
    waypoints_init();    // own NVS partition, see main/waypoints.c -- before ui_shell_start() for the same reason (Goto's saved list is built at creation time)

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

    battery_init();  // INA226 on the shared I2C bus -- see main/battery.c

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

    // hosted_bringup() already ran above, before nvs_flash_init() -- this
    // just reads any stored credentials (components/wifi_prov's own NVS
    // namespace) and kicks a background reconnect if there are any. Needs
    // ui_wifi()/ui_settings() to exist already (ui_shell_start() -> ui_init()
    // above built them). Skipped entirely if the P4<->C6 link itself never
    // came up (see hosted_bringup()) -- esp_wifi_remote has no working
    // transport under it in that case, so there's nothing for this to do.
    if (wifi_hw_ready) wifi_ui_bridge_init();

    fb_capture_start(); // on-demand screen capture over USB -- see main/fb_capture.c
#endif
}
