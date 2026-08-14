// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Wire protocol, PC -> ESP32:
//   SNAP GET\n
//   TAB <HOME|MAP|NAV|TELEMETRY|MORE>\n
// ESP32 -> PC:
//   SNAP FAIL <reason>\n                       (map screen up, lock busy, etc.)
//   SNAP OK <width> <height> <bytes> <crc32_hex>\n
//   TAB FAIL <reason>\n                        (unknown name, lock busy, can't leave Map remotely)
//   TAB OK\n
//
// TAB drives the same ui_show_tab() a real navbar tap does, so a capture
// can be scripted end-to-end (switch screen, then SNAP GET) with no one
// needed at the device. One exception: leaving the Map screen. Getting
// *onto* Map goes through ui_show_tab() same as any other tab, but leaving
// it needs ui_shell_return_to_tab() instead (LVGL is stopped while Map's
// native renderer owns the panel), which ui_shell.h documents as only
// safe to call from the native map task's own thread, right before that
// task deletes itself -- calling it from this task instead would resume
// LVGL while map_view.c's task is still running and still touching the
// panel, the exact "two renderers fighting over the framebuffer" failure
// mode ui_shell.c's own file header describes hitting once already. Rather
// than risk that unverified, TAB just fails cleanly when asked to leave a
// live Map screen -- a real tap on the map's own navbar is still the way
// off it for now.
//
// Both directions are short, single writes -- no bulk data goes over this
// channel at all. An earlier version streamed the captured frame back over
// this same USB-Serial-JTAG connection in base64 chunks and reliably wedged
// the task forever partway through on real hardware (confirmed: not a
// stack-size issue -- fixed that too along the way -- and not simple log
// interleaving either -- silencing ESP_LOG for the whole transfer didn't
// help). Root cause never fully pinned down; rather than keep chasing it,
// the frame now goes to the "snapshot" flash partition (see partitions.csv)
// via esp_partition_write() instead, and gets pulled off with
// `esptool read_flash` -- the same proven bootloader-mode path already used
// for flashing, not a custom live-transfer protocol. See
// tools/pull_snapshot.py for the PC-side retrieval + RGB565->PNG step.
//
// Text-line framing for the command/ack exchange, same reasoning as
// main/sd_xfer.c's XFER protocol: this task shares the USB-Serial-JTAG
// channel with ESP_LOG's secondary-console output (unlike sd_xfer.c's
// dedicated dev-tool mode, this one runs alongside the normal running app),
// so a stray log line landing between frames just fails to match "SNAP "
// and gets ignored by the PC side instead of corrupting a binary frame.

#include "fb_capture.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "design_ui.h"
#include "ui_common.h"
#include "ui_shell.h"
#include "ui_theme.h"

static const char *TAG = "FB_CAPTURE";

// Flash sector size on every ESP32 variant -- not pulled from a header
// (SPI_FLASH_SEC_SIZE lives in a deprecated/legacy header not otherwise
// used in this codebase).
#define FLASH_SECTOR_SIZE 4096

static uint16_t *s_scratch; // UI_SCREEN_W x UI_SCREEN_H, PSRAM, lazily allocated

// --- Raw USB-Serial-JTAG I/O -- see sd_xfer.c's identical helpers for why
// this bypasses stdio/console entirely and retries short writes. Duplicated
// here rather than shared: fb_capture.c and sd_xfer.c are used in mutually
// exclusive builds (dev-tool mode vs. normal mode) today, so there's no
// live caller that would benefit from a shared header, and matches how
// usb_msc.c/sd_xfer.c already each stand alone.

#define USJ_READ_BUF_SIZE 256
static uint8_t s_read_buf[USJ_READ_BUF_SIZE];
static size_t  s_read_pos = 0;
static size_t  s_read_len = 0;
static char    s_line[64]; // "SNAP GET" is the only inbound line; generous but bounded

static void usj_write_all(const uint8_t *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = usb_serial_jtag_write_bytes(data + sent, len - sent, portMAX_DELAY);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        sent += (size_t)n;
    }
}

static void usj_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    usj_write_all((const uint8_t *)buf, strlen(buf));
}

static bool read_line(void)
{
    size_t len = 0;
    while (1) {
        if (s_read_pos >= s_read_len) {
            int n = usb_serial_jtag_read_bytes(s_read_buf, USJ_READ_BUF_SIZE, portMAX_DELAY);
            if (n <= 0) continue;
            s_read_len = (size_t)n;
            s_read_pos = 0;
        }
        uint8_t c = s_read_buf[s_read_pos++];
        if (c == '\n') {
            s_line[len] = '\0';
            return true;
        }
        if (c != '\r' && len < sizeof(s_line) - 1) {
            s_line[len++] = (char)c;
        }
        // lines longer than s_line just get truncated -- fine, "SNAP GET"
        // is well under the limit and anything else is ignored anyway
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1) + 1));
        }
    }
    return ~crc;
}

// Snapshots lv_screen_active() into s_scratch (tightly packed, UI_SCREEN_W *
// UI_SCREEN_H * 2 bytes), then writes it to the "snapshot" flash partition
// (see partitions.csv) and acks with its size/crc32 -- see this file's
// header comment for why it's flash instead of a live transfer. Sends its
// own SNAP FAIL on any failure; caller just returns to the read loop.
static bool do_capture(void)
{
    if (ui_shell_map_active()) {
        usj_printf("SNAP FAIL map screen not supported yet\n");
        return false;
    }

    if (!s_scratch) {
        s_scratch = heap_caps_malloc((size_t)UI_SCREEN_W * UI_SCREEN_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        if (!s_scratch) {
            ESP_LOGE(TAG, "PSRAM alloc failed");
            usj_printf("SNAP FAIL out of memory\n");
            return false;
        }
    }

    // Generous timeout, not 0 (unlike ui_shell.c's own try-lock calls) --
    // this task isn't on any latency-sensitive path (touch/flush), so it's
    // fine to wait out whatever else briefly holds the lock rather than
    // failing the capture on the first busy tick.
    if (!lvgl_port_lock(1000)) {
        usj_printf("SNAP FAIL lvgl busy\n");
        return false;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_draw_buf_t *snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
    if (!snap) {
        lvgl_port_unlock();
        ESP_LOGE(TAG, "lv_snapshot_take failed");
        usj_printf("SNAP FAIL snapshot failed\n");
        return false;
    }

    // Row-by-row using the snapshot's own stride, same reasoning as
    // navbar_snapshot.c -- lv_snapshot_take() doesn't guarantee
    // stride == w*2.
    for (int y = 0; y < UI_SCREEN_H; y++) {
        const uint8_t *src_row = snap->data + (size_t)y * snap->header.stride;
        memcpy(s_scratch + (size_t)y * UI_SCREEN_W, src_row, (size_t)UI_SCREEN_W * sizeof(uint16_t));
    }

    lv_draw_buf_destroy(snap);
    lvgl_port_unlock();

    const size_t total = (size_t)UI_SCREEN_W * UI_SCREEN_H * sizeof(uint16_t);
    uint32_t crc = crc32_update(0, (const uint8_t *)s_scratch, total);
    ESP_LOGI(TAG, "captured %dx%d screen (%u bytes, crc32=%08lx), writing to flash",
             UI_SCREEN_W, UI_SCREEN_H, (unsigned)total, (unsigned long)crc);

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "snapshot");
    if (!part) {
        ESP_LOGE(TAG, "'snapshot' partition not found -- check partitions.csv");
        usj_printf("SNAP FAIL no snapshot partition\n");
        return false;
    }
    if (total > part->size) {
        ESP_LOGE(TAG, "capture (%u bytes) doesn't fit in the snapshot partition (%u bytes)",
                 (unsigned)total, (unsigned)part->size);
        usj_printf("SNAP FAIL capture too big for partition\n");
        return false;
    }

    // Erase in whole 4K sectors covering the write -- esp_partition_erase_range()
    // requires sector-aligned size.
    size_t erase_size = (total + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    esp_err_t err = esp_partition_erase_range(part, 0, erase_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_erase_range failed: %s", esp_err_to_name(err));
        usj_printf("SNAP FAIL flash erase failed\n");
        return false;
    }
    err = esp_partition_write(part, 0, s_scratch, total);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_write failed: %s", esp_err_to_name(err));
        usj_printf("SNAP FAIL flash write failed\n");
        return false;
    }

    ESP_LOGI(TAG, "wrote %u bytes to the snapshot partition", (unsigned)total);
    usj_printf("SNAP OK %d %d %u %08lx\n", UI_SCREEN_W, UI_SCREEN_H, (unsigned)total, (unsigned long)crc);
    return true;
}

// Switches screens the same way a real navbar tap does -- see this file's
// header comment for the one thing it deliberately won't do (leave a live
// Map screen).
static void do_tab(const char *name)
{
    ui_tab_t tab;
    if      (!strcmp(name, "HOME"))      tab = UI_TAB_HOME;
    else if (!strcmp(name, "MAP"))       tab = UI_TAB_MAP;
    else if (!strcmp(name, "NAV"))       tab = UI_TAB_NAV;
    else if (!strcmp(name, "TELEMETRY")) tab = UI_TAB_TELEMETRY;
    else if (!strcmp(name, "MORE"))      tab = UI_TAB_MORE;
    else {
        usj_printf("TAB FAIL unknown tab '%s'\n", name);
        return;
    }

    if (ui_shell_map_active() && tab != UI_TAB_MAP) {
        usj_printf("TAB FAIL can't leave Map remotely yet -- tap its navbar\n");
        return;
    }

    if (!lvgl_port_lock(1000)) {
        usj_printf("TAB FAIL lvgl busy\n");
        return;
    }
    ui_show_tab(tab);
    lvgl_port_unlock();
    usj_printf("TAB OK\n");
}

static void fb_capture_task(void *arg)
{
    (void)arg;

    // The normal-mode build's console has USB-Serial-JTAG only as a
    // *secondary* (write-only) console (see sdkconfig.defaults) -- unlike
    // sd_xfer.c's dedicated dev-tool mode, which reconfigures it as
    // *primary* specifically so the driver gets installed for it. Confirmed
    // on real hardware this call succeeds fresh here (ESP_OK, no
    // already-installed warning) -- the secondary console's own output
    // doesn't go through this installable driver, so there's no ownership
    // conflict to handle.
    usb_serial_jtag_driver_config_t usj_cfg = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 1024,
    };
    esp_err_t err = usb_serial_jtag_driver_install(&usj_cfg);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "usb_serial_jtag driver already installed (likely by the console) -- using it as-is");
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: %s -- capture unavailable", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "ready, waiting for SNAP GET / TAB <name>");
    while (1) {
        read_line();
        if (strcmp(s_line, "SNAP GET") == 0) {
            do_capture();
        } else if (!strncmp(s_line, "TAB ", 4)) {
            do_tab(s_line + 4);
        }
        // any other line (including interleaved log output) is ignored
    }
}

void fb_capture_start(void)
{
    // Stack: 4096 bytes wasn't enough -- confirmed on real hardware: a
    // full-screen lv_snapshot_take() (unlike navbar_snapshot.c's much
    // shallower navbar-only capture) recurses deep enough through LVGL's
    // render tree to overflow it (Guru Meditation Error, Stack protection
    // fault, "Detected in task \"fb_capture\""). 16K is a generous margin
    // above that, not itself measured precisely -- fine to right-size later
    // if it ever matters. Plain priority/no core pinning is fine now that
    // there's no bulk transfer competing for scheduling.
    xTaskCreate(fb_capture_task, "fb_capture", 16384, NULL, 3, NULL);
}
