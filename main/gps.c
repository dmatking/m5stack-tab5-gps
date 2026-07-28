// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// GPS NMEA reader for the M5Stack GPS Module v2.1 (AT6668) on the Tab5's
// M5-Bus connector. Adapted from esp32-idf-new/modules/gps/_common/gps.c
// (a chip-agnostic NMEA parser reused across this project family) --
// see gps.h for what changed. Parses $G[NP]GGA (fix/position/time/sat
// count) and $G[NP]RMC (fix/position/speed/heading/date) into a
// thread-safe snapshot; anything else (GSA/GSV/GLL/ZDA/TXT) is ignored,
// same as upstream.
//
// Pins/power confirmed working on real hardware 2026-07-27 -- see
// [[reference-gps-module-hw]]: UART1 (not UART0, that's the console),
// RX=GPIO38, TX=GPIO37 (Tab5 M5-Bus RXD0/TXD0), 115200 8N1, module's own
// DIP switch 6 (not the "TXD"-labeled group as the silkscreen suggests --
// verified empirically after the labeled group produced nothing).

#include "gps.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define GPS_UART_NUM UART_NUM_1
#define GPS_RX_GPIO  GPIO_NUM_38  // Tab5 M5-Bus RXD0 (bus pin 13)
#define GPS_TX_GPIO  GPIO_NUM_37  // Tab5 M5-Bus TXD0 (bus pin 14) -- unused
#define GPS_BAUD     115200

#define GPS_LINE_MAX 128

static const char *TAG = "GPS";

static gps_state_t s_state;
static SemaphoreHandle_t s_mutex;

// ---------------------------------------------------------------------------
// NMEA field parsers
// ---------------------------------------------------------------------------

static bool parse_hhmmss(const char *field, int *hour, int *minute, int *second)
{
    if (!field || strlen(field) < 6 || !isdigit((unsigned char)field[0])) {
        return false;
    }
    *hour   = (field[0] - '0') * 10 + (field[1] - '0');
    *minute = (field[2] - '0') * 10 + (field[3] - '0');
    *second = (field[4] - '0') * 10 + (field[5] - '0');
    return true;
}

static bool parse_ddmmyy(const char *field, struct tm *out)
{
    if (!field || strlen(field) < 6) {
        return false;
    }
    int day   = (field[0] - '0') * 10 + (field[1] - '0');
    int month = (field[2] - '0') * 10 + (field[3] - '0');
    int year  = (field[4] - '0') * 10 + (field[5] - '0');
    if (day <= 0 || month <= 0) {
        return false;
    }
    out->tm_mday  = day;
    out->tm_mon   = month - 1;
    out->tm_year  = (year >= 70 ? 1900 : 2000) + year - 1900;
    out->tm_isdst = 0;
    return true;
}

static bool parse_lat_lon(const char *lat_field, const char *ns_field,
                          const char *lon_field, const char *ew_field,
                          double *lat_deg, double *lon_deg)
{
    if (!lat_field || !lon_field || !ns_field || !ew_field ||
        lat_field[0] == '\0' || lon_field[0] == '\0') {
        return false;
    }
    double lat_raw    = strtod(lat_field, NULL);
    double lon_raw    = strtod(lon_field, NULL);
    int    lat_deg_i  = (int)(lat_raw / 100.0);
    int    lon_deg_i  = (int)(lon_raw / 100.0);
    double lat        = lat_deg_i + (lat_raw - lat_deg_i * 100.0) / 60.0;
    double lon        = lon_deg_i + (lon_raw - lon_deg_i * 100.0) / 60.0;
    if (ns_field[0] == 'S') lat = -lat;
    if (ew_field[0] == 'W') lon = -lon;
    *lat_deg = lat;
    *lon_deg = lon;
    return true;
}

// ---------------------------------------------------------------------------
// Sentence handlers (operate on a local copy, then lock to update state)
// ---------------------------------------------------------------------------

static void handle_gga(char *sentence)
{
    char *sp = NULL;
    (void)strtok_r(sentence, ",", &sp);         // $GxGGA
    char *utc  = strtok_r(NULL, ",", &sp);
    char *lat  = strtok_r(NULL, ",", &sp);
    char *ns   = strtok_r(NULL, ",", &sp);
    char *lon  = strtok_r(NULL, ",", &sp);
    char *ew   = strtok_r(NULL, ",", &sp);
    char *fix  = strtok_r(NULL, ",", &sp);
    char *sats = strtok_r(NULL, ",", &sp);

    gps_state_t tmp;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    tmp = s_state;
    xSemaphoreGive(s_mutex);

    int h, m, s;
    if (parse_hhmmss(utc, &h, &m, &s)) {
        tmp.utc_tm.tm_hour = h;
        tmp.utc_tm.tm_min  = m;
        tmp.utc_tm.tm_sec  = s;
        tmp.utc_tm.tm_isdst = 0;
        tmp.time_valid = true;
    }

    double lat_d = 0.0, lon_d = 0.0;
    if (parse_lat_lon(lat, ns, lon, ew, &lat_d, &lon_d)) {
        tmp.latitude_deg  = lat_d;
        tmp.longitude_deg = lon_d;
        tmp.latlon_valid  = true;
    }

    tmp.gga_fix = fix && atoi(fix) > 0;
    if (sats && sats[0] != '\0') {
        tmp.sats_in_use = atoi(sats);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = tmp;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "GGA fix=%d sats=%d lat=%.5f lon=%.5f",
             tmp.gga_fix, tmp.sats_in_use, tmp.latitude_deg, tmp.longitude_deg);
}

static void handle_rmc(char *sentence)
{
    char *sp = NULL;
    (void)strtok_r(sentence, ",", &sp);         // $GxRMC
    char *utc    = strtok_r(NULL, ",", &sp);
    char *status = strtok_r(NULL, ",", &sp);
    char *lat    = strtok_r(NULL, ",", &sp);
    char *ns     = strtok_r(NULL, ",", &sp);
    char *lon    = strtok_r(NULL, ",", &sp);
    char *ew     = strtok_r(NULL, ",", &sp);
    char *speed  = strtok_r(NULL, ",", &sp);
    char *track  = strtok_r(NULL, ",", &sp);
    char *date   = strtok_r(NULL, ",", &sp);

    gps_state_t tmp;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    tmp = s_state;
    xSemaphoreGive(s_mutex);

    int h, m, s;
    if (parse_hhmmss(utc, &h, &m, &s)) {
        tmp.utc_tm.tm_hour  = h;
        tmp.utc_tm.tm_min   = m;
        tmp.utc_tm.tm_sec   = s;
        tmp.utc_tm.tm_isdst = 0;
        tmp.time_valid = true;
    }

    if (parse_ddmmyy(date, &tmp.utc_tm)) {
        tmp.date_valid = true;
    }

    double lat_d = 0.0, lon_d = 0.0;
    if (parse_lat_lon(lat, ns, lon, ew, &lat_d, &lon_d)) {
        tmp.latitude_deg  = lat_d;
        tmp.longitude_deg = lon_d;
        tmp.latlon_valid  = true;
    }

    tmp.rmc_fix = status && (*status == 'A' || *status == 'D');

    if (speed && speed[0] != '\0') {
        tmp.speed_knots = strtof(speed, NULL);
        tmp.speed_valid = true;
    }
    if (track && track[0] != '\0') {
        tmp.heading_deg = strtof(track, NULL);
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = tmp;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "RMC fix=%d spd=%.1f hdg=%.1f",
             tmp.rmc_fix, tmp.speed_knots, tmp.heading_deg);
}

static void handle_sentence(char *sentence)
{
    if (!sentence || sentence[0] != '$') {
        return;
    }
    if (!strncmp(sentence, "$GNGGA", 6) || !strncmp(sentence, "$GPGGA", 6)) {
        handle_gga(sentence);
    } else if (!strncmp(sentence, "$GNRMC", 6) || !strncmp(sentence, "$GPRMC", 6)) {
        handle_rmc(sentence);
    }
}

// ---------------------------------------------------------------------------
// Reader task
// ---------------------------------------------------------------------------

static void gps_reader_task(void *arg)
{
    (void)arg;
    uint8_t buf[64];
    char    line[GPS_LINE_MAX];
    size_t  line_len = 0;

    ESP_LOGI(TAG, "reader task started on UART%d", (int)GPS_UART_NUM);

    while (1) {
        int len = uart_read_bytes(GPS_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(1000));
        if (len <= 0) {
            continue;
        }
        for (int i = 0; i < len; ++i) {
            char c = (char)buf[i];
            if (c == '\r' || c == '\n') {
                if (line_len > 0) {
                    line[line_len] = '\0';
                    handle_sentence(line);
                    line_len = 0;
                }
            } else {
                if (line_len < GPS_LINE_MAX - 1) {
                    line[line_len++] = c;
                } else {
                    line_len = 0;  // overflow -- drop line
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void gps_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex);

    const uart_config_t cfg = {
        .baud_rate  = GPS_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, 4096, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_GPIO, GPS_RX_GPIO,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART%d TX=%d RX=%d %d baud",
             (int)GPS_UART_NUM, GPS_TX_GPIO, GPS_RX_GPIO, GPS_BAUD);

    xTaskCreate(gps_reader_task, "gps_reader", 4096, NULL, 5, NULL);
}

gps_state_t gps_get_state(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    gps_state_t copy = s_state;
    xSemaphoreGive(s_mutex);
    return copy;
}

bool gps_has_fix(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool fix = s_state.gga_fix || s_state.rmc_fix;
    xSemaphoreGive(s_mutex);
    return fix;
}
