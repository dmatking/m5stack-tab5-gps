// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// USB mass-storage (MSC) mode: exposes the microSD card as a normal USB
// drive over the Tab5's USB-A/OTG port, using espressif/esp_tinyusb's
// tinyusb_msc component (which owns the card's FATFS-mount lifecycle
// itself -- unlike sd_card.c's all-in-one esp_vfs_fat_sdmmc_mount(), this
// does a raw SDMMC card init and hands the card handle to
// tinyusb_msc_new_storage_sdmmc(), mirroring ESP-IDF's own
// examples/peripherals/usb/device/tusb_msc P4+SDMMC example.
//
// SDMMC pin/power config is identical to sd_card.c (see that file for the
// M5Stack BSP cross-reference) -- this is a separate, parallel init rather
// than reusing sd_card.c's mount, because only one thing can own the card's
// FATFS mount at a time, and tinyusb_msc needs raw card ownership to
// arbitrate app-vs-USB access.

#include "usb_msc.h"

#include <stdio.h>
#include <stdlib.h>

#include "board_interface.h"
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

static const char *TAG = "USB_MSC";

#define SD_GPIO_CLK  GPIO_NUM_43
#define SD_GPIO_CMD  GPIO_NUM_44
#define SD_GPIO_D0   GPIO_NUM_39
#define SD_GPIO_D1   GPIO_NUM_40
#define SD_GPIO_D2   GPIO_NUM_41
#define SD_GPIO_D3   GPIO_NUM_42
#define SD_BUS_WIDTH 4
#define SD_LDO_CHAN  4

// --- TinyUSB descriptors (MSC-only device, no CDC -- COM17/USB-Serial-JTAG
// already gives console access independently on its own PHY) ---

#define EPNUM_MSC            1
#define TUSB_DESC_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum { ITF_NUM_MSC = 0, ITF_NUM_TOTAL };
enum { EDPT_CTRL_OUT = 0x00, EDPT_CTRL_IN = 0x80, EDPT_MSC_OUT = 0x01, EDPT_MSC_IN = 0x81 };

static const tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(s_device_desc),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,  // Espressif VID -- hobby/dev use only, not a product
    .idProduct          = 0x4002,
    .bcdDevice          = 0x100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

static const uint8_t s_fs_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

#if (TUD_OPT_HIGH_SPEED)
static const tusb_desc_device_qualifier_t s_device_qualifier = {
    .bLength            = sizeof(tusb_desc_device_qualifier_t),
    .bDescriptorType    = TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .bNumConfigurations = 0x01,
    .bReserved          = 0,
};

static const uint8_t s_hs_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 512),
};
#endif  // TUD_OPT_HIGH_SPEED

static const char *s_string_desc[] = {
    (const char[]){ 0x09, 0x04 },  // English (0x0409)
    "David King",
    "Tab5 SD Transfer",
    "1",
    "Map Tiles",
};

static void storage_mount_changed_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event, void *arg)
{
    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE:
        ESP_LOGI(TAG, "storage mounted to: %s", (event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) ? "app" : "USB");
        break;
    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        ESP_LOGE(TAG, "storage mount failed or format required");
        break;
    default:
        break;
    }
}

static bool sdmmc_card_raw_init(sdmmc_card_t **out_card)
{
    sd_pwr_ctrl_ldo_config_t ldo_cfg = { .ldo_chan_id = SD_LDO_CHAN };
    sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
    esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr_ctrl_handle);
    if (err != ESP_OK) {
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

    sdmmc_card_t *card = malloc(sizeof(sdmmc_card_t));
    if (!card) {
        ESP_LOGE(TAG, "could not allocate sdmmc_card_t");
        return false;
    }

    err = (*host.init)();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC host init failed: %d", err);
        free(card);
        return false;
    }

    err = sdmmc_host_init_slot(host.slot, (const sdmmc_slot_config_t *)&slot_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SDMMC slot init failed: %d", err);
        free(card);
        return false;
    }

    err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card init failed (no card / bad card / %d)", err);
        free(card);
        return false;
    }

    sdmmc_card_print_info(stdout, card);
    *out_card = card;
    return true;
}

bool usb_msc_start(void)
{
    // Must happen before anything else touches the OTG port -- otherwise
    // this board's own 5V boost fights the host PC's VBUS on the same line.
    board_set_usb5v_en(false);

    sdmmc_card_t *card = NULL;
    if (!sdmmc_card_raw_init(&card)) {
        return false;
    }

    tinyusb_msc_storage_config_t storage_cfg = {
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,  // exposed to the PC immediately
        .fat_fs = {
            .base_path       = NULL,
            .config.max_files = 5,
            .format_flags    = 0,
        },
    };
    storage_cfg.medium.card = card;

    tinyusb_msc_storage_handle_t storage_hdl = NULL;
    esp_err_t err = tinyusb_msc_new_storage_sdmmc(&storage_cfg, &storage_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_msc_new_storage_sdmmc failed: %d", err);
        return false;
    }
    ESP_ERROR_CHECK(tinyusb_msc_set_storage_callback(storage_mount_changed_cb, NULL));

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = &s_device_desc;
    tusb_cfg.descriptor.full_speed_config = s_fs_config_desc;
    tusb_cfg.descriptor.string = s_string_desc;
    tusb_cfg.descriptor.string_count = sizeof(s_string_desc) / sizeof(s_string_desc[0]);
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = s_hs_config_desc;
    tusb_cfg.descriptor.qualifier = &s_device_qualifier;
#endif

    err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %d", err);
        return false;
    }

    ESP_LOGI(TAG, "USB mass storage ready -- connect USB-A to a PC via an A-to-A cable");
    return true;
}
