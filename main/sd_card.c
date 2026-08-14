// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Mounts the Tab5's microSD card (SDMMC, 4-bit bus, dedicated host slot 0)
// as a FAT filesystem at SD_MOUNT_POINT. Pin assignments and the on-chip
// LDO power-up sequence are taken directly from M5Stack's own reference BSP
// (M5Tab5-UserDemo/platforms/tab5/components/m5stack_tab5/m5stack_tab5.c,
// bsp_sdcard_init()) and cross-checked against ESP-IDF's own
// SDMMC_SLOT_CONFIG_DEFAULT() for CONFIG_IDF_TARGET_ESP32P4, which already
// matches exactly -- the P4's SDMMC pins are IOMUX-fixed, not GPIO-matrix-
// routable, so there's no board-specific pin choice to get wrong here.
//
// This is a *different* power rail than EXT5V_EN (see board_m5stack_tab5.c's
// pi4ioe_init(), which unconditionally enables EXT5V_EN early via the I2C
// IO expander): SD card IO power comes from the ESP32-P4's own on-chip LDO
// channel 4, via sd_pwr_ctrl_new_on_chip_ldo(). The two are unrelated --
// don't fold this into pi4ioe_init().
//
// Never auto-formats the card (format_if_mount_failed = false) -- a failed
// mount (no card inserted, unformatted/corrupt card, wrong filesystem) just
// disables SD access for this boot; it is not treated as fatal. There's no
// card-detect pin wired on this board, so a failed mount is the only signal
// for "no card" -- there's no cheap pre-check.

#include "sd_card.h"

#include <stdio.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG = "SD_CARD";

// Pin assignments (M5Stack Tab5 official BSP: m5stack_tab5.h BSP_SD_*,
// m5stack_tab5.c GPIO_SDMMC_*) -- fixed hardware wiring, listed explicitly
// here for self-documentation even though they match the P4's own
// SDMMC_SLOT_CONFIG_DEFAULT() already.
#define SD_GPIO_CLK  GPIO_NUM_43
#define SD_GPIO_CMD  GPIO_NUM_44
#define SD_GPIO_D0   GPIO_NUM_39
#define SD_GPIO_D1   GPIO_NUM_40
#define SD_GPIO_D2   GPIO_NUM_41
#define SD_GPIO_D3   GPIO_NUM_42
#define SD_BUS_WIDTH 4

#define SD_LDO_CHAN  4  // on-chip LDO_VO4 powers the SD IO rail
#define SD_MAX_FILES 4

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

bool sd_card_mount(void)
{
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = SD_LDO_CHAN };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_ctrl_handle);
    if (err != ESP_OK) {
        // A failure here means the LDO peripheral itself is misconfigured --
        // a real hardware/programming error, not "no card inserted".
        ESP_LOGE(TAG, "sd_pwr_ctrl_new_on_chip_ldo failed: %d", err);
        return false;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    sdmmc_slot_config_t slot_cfg = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_cfg.width = SD_BUS_WIDTH;
    slot_cfg.clk = SD_GPIO_CLK;
    slot_cfg.cmd = SD_GPIO_CMD;
    slot_cfg.d0 = SD_GPIO_D0;
    slot_cfg.d1 = SD_GPIO_D1;
    slot_cfg.d2 = SD_GPIO_D2;
    slot_cfg.d3 = SD_GPIO_D3;
    // No card-detect/write-protect pins wired on this board.

    esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,  // never auto-format the user's card
        .max_files              = SD_MAX_FILES,
        .allocation_unit_size   = 16 * 1024,
    };

    err = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card mount failed (no card / bad card / %d) -- SD unavailable", err);
        return false;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card mounted at %s:", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, s_card);
    return true;
}

bool sd_card_is_mounted(void)
{
    return s_mounted;
}

bool sd_card_get_usage(float *used_gb, float *total_gb)
{
    if (!s_mounted) return false;

    uint64_t total_bytes = 0, free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info(SD_MOUNT_POINT, &total_bytes, &free_bytes);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_vfs_fat_info failed: %s", esp_err_to_name(err));
        return false;
    }

    if (used_gb)  *used_gb  = (float)((double)(total_bytes - free_bytes) / 1e9);
    if (total_gb) *total_gb = (float)((double)total_bytes / 1e9);
    return true;
}
