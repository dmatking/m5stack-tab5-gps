// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Line-based file transfer over the console (COM17/USB-Serial-JTAG),
// fallback to main/usb_msc.c when USB mass storage proved unreliable over
// the available cable. Two directions, both text/line-based rather than a
// raw binary protocol on the same channel esp_log also writes to -- a stray
// log line interleaved mid-transfer just fails to match "XFER " and gets
// ignored by the PC side, instead of corrupting a binary frame.
//
// Upload, PC -> SD card (see tools/send_to_sd.py):
//   PC -> ESP32:
//     XFER BEGIN <filename> <size>\n
//     XFER DATA <base64 chunk>\n         (repeated)
//     XFER END <crc32_hex>\n
//   ESP32 -> PC:
//     XFER OK <bytes_written> <crc32_hex>\n
//     XFER FAIL <reason>\n
//
// Download, SD card -> PC (see tools/recv_from_sd.py):
//   PC -> ESP32:
//     XFER GET <filename>\n
//   ESP32 -> PC:
//     XFER SIZE <size>\n
//     XFER DATA <base64 chunk>\n         (repeated)
//     XFER END <crc32_hex>\n
//     XFER FAIL <reason>\n               (instead of SIZE, e.g. file not found)
//
// Common:
//   ESP32 -> PC:
//     XFER READY\n                        (at boot, and after each transfer)

#include "sd_xfer.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sd_card.h"

static const char *TAG = "SD_XFER";

// Raw bytes per line, divisible by 3 so every full chunk's base64 has no
// '=' padding to worry about mid-stream (only the final, possibly-short
// chunk may be padded).
#define RAW_CHUNK_SIZE   6000
#define B64_CHUNK_SIZE   (RAW_CHUNK_SIZE * 4 / 3 + 4)
#define LINE_BUF_SIZE    (B64_CHUNK_SIZE + 32)

static char    s_line[LINE_BUF_SIZE];
static uint8_t s_raw[RAW_CHUNK_SIZE];

// --- Raw USB-Serial-JTAG I/O, bypassing stdio/console entirely. The
// console's stdin (fgets/scanf) turned out not to reliably follow reads
// once this driver is installed mid-boot -- writes (incl. ESP_LOG) kept
// working, but nothing sent by the PC ever reached fgets(). Talking to the
// driver directly sidesteps that ambiguity. ESP_LOG is still used for
// internal diagnostics (goes out via the console's own path, unaffected).

#define USJ_READ_BUF_SIZE 512
static uint8_t s_read_buf[USJ_READ_BUF_SIZE];
static size_t  s_read_pos = 0;
static size_t  s_read_len = 0;

// usb_serial_jtag_write_bytes() returns the number of bytes actually
// written and is short-write-capable -- its own header says blocking mode
// only blocks "for a short period if the TX FIFO is full", not until the
// whole request is drained. The 4096-byte TX ring is smaller than a full
// base64 DATA line (~8000 bytes for RAW_CHUNK_SIZE=6000), so a single call
// silently dropped the back half of large writes until this loop was added
// (confirmed on real hardware: every full-size download chunk decoded to 0
// bytes on the PC side, while the final undersized chunk came through fine).
static void usj_write_all(const uint8_t *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = usb_serial_jtag_write_bytes(data + sent, len - sent, portMAX_DELAY);
        if (n <= 0) {
            // A busy `continue` here (no yield) starved the USB stack's own
            // interrupt servicing badly enough that Windows lost the device
            // mid-transfer ("ClearCommError failed") -- same class of bug as
            // [[feedback-generator-task-pacing]]: bulk transfers need a real
            // vTaskDelay, not a tight spin, to let the rest of the system run.
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        sent += (size_t)n;
    }
}

static void usj_write_str(const char *s)
{
    usj_write_all((const uint8_t *)s, strlen(s));
}

static void usj_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    usj_write_str(buf);
}

// Blocks until a full line (newline-terminated) is available, then returns
// true; strips the trailing \r/\n. Always blocks until it gets one -- bool
// return is just so callers can use it directly in a while() condition.
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
        if (c != '\r' && len < LINE_BUF_SIZE - 1) {
            s_line[len++] = (char)c;
        }
    }
}

// --- CRC32 (reflected, poly 0xEDB88320, init/final 0xFFFFFFFF -- matches
// Python's zlib.crc32()) ---
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

// --- Minimal base64 decoder (standard alphabet, '=' padding) ---
static int b64_decode(const char *in, size_t in_len, uint8_t *out)
{
    static int8_t table[256];
    static bool table_ready = false;
    if (!table_ready) {
        memset(table, -1, sizeof(table));
        const char *alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) table[(uint8_t)alpha[i]] = (int8_t)i;
        table_ready = true;
    }

    int out_len = 0;
    uint32_t acc = 0;
    int acc_bits = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = in[i];
        if (c == '=' || c == '\0') break;
        int8_t v = table[(uint8_t)c];
        if (v < 0) continue;  // skip whitespace/newline stragglers
        acc = (acc << 6) | (uint32_t)v;
        acc_bits += 6;
        if (acc_bits >= 8) {
            acc_bits -= 8;
            out[out_len++] = (uint8_t)((acc >> acc_bits) & 0xFF);
        }
    }
    return out_len;
}

// --- Minimal base64 encoder (standard alphabet, '=' padding) ---
static int b64_encode(const uint8_t *in, size_t in_len, char *out)
{
    static const char *alpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0;
    int out_len = 0;
    for (; i + 3 <= in_len; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[out_len++] = alpha[(v >> 18) & 0x3F];
        out[out_len++] = alpha[(v >> 12) & 0x3F];
        out[out_len++] = alpha[(v >> 6) & 0x3F];
        out[out_len++] = alpha[v & 0x3F];
    }
    size_t rem = in_len - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[out_len++] = alpha[(v >> 18) & 0x3F];
        out[out_len++] = alpha[(v >> 12) & 0x3F];
        out[out_len++] = '=';
        out[out_len++] = '=';
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[out_len++] = alpha[(v >> 18) & 0x3F];
        out[out_len++] = alpha[(v >> 12) & 0x3F];
        out[out_len++] = alpha[(v >> 6) & 0x3F];
        out[out_len++] = '=';
    }
    return out_len;
}

static void do_download(const char *filename)
{
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, filename);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        usj_printf("XFER FAIL cannot open %s\n", filename);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    ESP_LOGI(TAG, "sending %s (%ld bytes)", filename, size);
    usj_printf("XFER SIZE %ld\n", size);

    // Whole "XFER DATA <b64>\n" line built in one buffer and sent with a
    // single usj_write_all() call -- three separate writes (prefix/payload/
    // newline) left a window where a log line from another task could land
    // in between and split the frame; one call closes that window (down to
    // whatever usj_write_all's own internal retry loop still leaves open,
    // which in practice has been reliable).
    static uint8_t line_buf[10 + B64_CHUNK_SIZE + 1];
    memcpy(line_buf, "XFER DATA ", 10);

    uint32_t crc = 0;
    long total = 0;
    while (total < size) {
        size_t n = fread(s_raw, 1, RAW_CHUNK_SIZE, f);
        if (n == 0) break;
        crc = crc32_update(crc, s_raw, n);
        int b64_len = b64_encode(s_raw, n, (char *)line_buf + 10);
        line_buf[10 + b64_len] = '\n';
        usj_write_all(line_buf, 10 + (size_t)b64_len + 1);
        total += (long)n;
    }
    fclose(f);
    usj_printf("XFER END %08lx\n", (unsigned long)crc);
    ESP_LOGI(TAG, "%s sent: %ld bytes, crc32=%08lx", filename, total, (unsigned long)crc);
}

static void do_transfer(const char *filename, long expected_size)
{
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, filename);

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        usj_printf("XFER FAIL cannot open %s\n", filename);
        return;
    }

    uint32_t crc = 0;
    long total = 0;
    ESP_LOGI(TAG, "receiving %s (%ld bytes expected)", filename, expected_size);

    while (read_line()) {
        if (strncmp(s_line, "XFER DATA ", 10) == 0) {
            int n = b64_decode(s_line + 10, strlen(s_line + 10), s_raw);
            if (n > 0) {
                if (fwrite(s_raw, 1, (size_t)n, f) != (size_t)n) {
                    ESP_LOGE(TAG, "fwrite failed at offset %ld", total);
                    fclose(f);
                    usj_printf("XFER FAIL write error at %ld\n", total);
                    return;
                }
                crc = crc32_update(crc, s_raw, (size_t)n);
                total += n;
            }
        } else if (strncmp(s_line, "XFER END ", 9) == 0) {
            fclose(f);
            uint32_t expected_crc = (uint32_t)strtoul(s_line + 9, NULL, 16);
            if (total != expected_size) {
                ESP_LOGW(TAG, "size mismatch: got %ld, expected %ld", total, expected_size);
                usj_printf("XFER FAIL size mismatch got=%ld expected=%ld\n", total, expected_size);
                return;
            }
            if (crc != expected_crc) {
                ESP_LOGW(TAG, "CRC mismatch: got %08lx, expected %08lx", (unsigned long)crc, (unsigned long)expected_crc);
                usj_printf("XFER FAIL crc mismatch got=%08lx expected=%08lx\n", (unsigned long)crc, (unsigned long)expected_crc);
                return;
            }
            ESP_LOGI(TAG, "%s OK: %ld bytes, crc32=%08lx", filename, total, (unsigned long)crc);
            usj_printf("XFER OK %ld %08lx\n", total, (unsigned long)crc);
            return;
        }
        // any other line (including interleaved log output) is ignored
    }
}

void sd_xfer_run(void)
{
    // Raw driver install, no VFS/console involvement -- see the read_line()
    // comment above for why: stdin's fgets() never reliably saw incoming
    // bytes once this driver was installed mid-boot, even though writes
    // (ESP_LOG, printf) kept working fine.
    // rx_buffer_size must comfortably exceed one full "XFER DATA <b64>\n"
    // line (~8014 bytes at RAW_CHUNK_SIZE=6000) -- confirmed on real
    // hardware that leaving it at 4096 (smaller than a single line) causes
    // silent data loss: any brief stall on this task's side while a line
    // is still streaming in (fwrite() to the SD card is the obvious one --
    // FAT cluster allocation/metadata flushes can easily take tens of ms)
    // lets the ring wrap and overwrite bytes read_line() hasn't drained
    // yet, with no error raised anywhere -- every upload just silently
    // came up short, caught only by do_transfer()'s final size check.
    usb_serial_jtag_driver_config_t usj_cfg = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 16384,
    };
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj_cfg));

    if (!sd_card_mount()) {
        ESP_LOGE(TAG, "SD card not available -- nothing to do.");
        usj_printf("XFER FAIL no SD card\n");
        return;
    }

    while (1) {
        usj_printf("XFER READY\n");

        read_line();  // blocks until a full line arrives
        char filename[128];
        long size = 0;
        if (sscanf(s_line, "XFER BEGIN %127s %ld", filename, &size) == 2) {
            do_transfer(filename, size);
        } else if (sscanf(s_line, "XFER GET %127s", filename) == 1) {
            do_download(filename);
        }
        // any other line while idle (including log noise) is ignored
    }
}
