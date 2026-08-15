// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// GPS NMEA reader for the M5Stack GPS Module v2.1 (AT6668) on the Tab5's
// M5-Bus connector. Adapted from esp32-idf-new/modules/gps/_common/gps.c
// (a chip-agnostic NMEA parser reused across this project family) --
// see gps.h for what changed. Parses $G[NP]GGA (fix/position/time/sat
// count), $G[NP]RMC (fix/position/speed/heading/date), $GxGSA (DOP/fix
// type/used-satellite PRNs) and $GxGSV (per-satellite elevation/azimuth/
// SNR) into a thread-safe snapshot; anything else (GLL/ZDA/TXT) is
// ignored.
//
// Pins/power confirmed working on real hardware 2026-07-27 -- see
// [[reference-gps-module-hw]]: UART1 (not UART0, that's the console),
// RX=GPIO38, TX=GPIO37 (Tab5 M5-Bus RXD0/TXD0), 115200 8N1, module's own
// DIP switch 6 (not the "TXD"-labeled group as the silkscreen suggests --
// verified empirically after the labeled group produced nothing).

#include "gps.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sd_card.h"

#define GPS_UART_NUM UART_NUM_1
#define GPS_RX_GPIO  GPIO_NUM_38  // Tab5 M5-Bus RXD0 (bus pin 13)
#define GPS_TX_GPIO  GPIO_NUM_37  // Tab5 M5-Bus TXD0 (bus pin 14) -- unused
#define GPS_BAUD     115200

#define GPS_LINE_MAX 128
#define GPS_LOG_PATH SD_MOUNT_POINT "/gps_log.txt"

static const char *TAG = "GPS";

static gps_state_t s_state;
static SemaphoreHandle_t s_mutex;
static int64_t s_last_gga_us; // for fix_rate_hz -- see handle_gga()
static FILE *s_log_f;  // raw NMEA log on the SD card, NULL if unavailable
static volatile bool s_log_ok = false;  // most recent write (or the initial open) succeeded

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

// strtok_r() treats a run of consecutive delimiters as one -- fine for
// whitespace-separated text, wrong for NMEA, whose empty fields (",,,,,,")
// are common and semantically meaningful (field absent, not "field merged
// with its neighbor"). Using strtok_r here silently shifted every field
// after an empty one, e.g. letting HDOP leak into what should have been an
// empty longitude field. This walks the string manually instead, splitting
// on every comma and returning "" for empty fields rather than skipping them.
static char *next_field(char **cursor)
{
    if (!*cursor) return NULL;
    char *start = *cursor;
    char *comma = strchr(start, ',');
    if (comma) {
        *comma = '\0';
        *cursor = comma + 1;
    } else {
        *cursor = NULL;
    }
    return start;
}

// ---------------------------------------------------------------------------
// Multi-GNSS satellite tracking (GSV/GSA)
// ---------------------------------------------------------------------------
//
// GSV gives existence/PRN/elevation/azimuth/SNR, one talker per constellation
// (e.g. $GPGSV for GPS, $GLGSV for GLONASS), each constellation's list
// possibly split across several sentences (a "total messages"/"this message"
// pair) since only 4 satellites fit per sentence. GSA gives which PRNs are
// actually used in the fix -- this module emits one $GNGSA per constellation
// too, distinguished by a trailing system ID field (1=GPS, 2=GLONASS,
// 3=Galileo, 4=BeiDou, 5=QZSS), not by the (always "GN") talker prefix.
// Confirmed empirically against this exact module's real output (a
// deliberate step -- see [[feedback_derisk_hw_before_plumbing]] -- rather
// than assumed from the NMEA spec, since which of several valid multi-GNSS
// conventions a given receiver actually uses varies):
//   $GNGSA,A,1,,,,,,,,,,,,,25.5,25.5,25.5,1*01   (system ID 1 = GPS, no fix so no PRNs)
//   $GPGSV,2,1,05,04,19,168,,07,73,016,,08,51,055,,09,44,195,,1*6D
//   $GPGSV,2,2,05,27,17,042,,1*54                (2-sentence GPS group, 5 sats total)
//   $GLGSV,1,1,00,1*78                           (0 GLONASS sats in view right now)
//   $BDGSV,1,1,01,24,49,058,,1*42                (BeiDou, PRN 24, no SNR yet)

static gps_constellation_t talker_to_constellation(const char *sentence)
{
    // sentence[1..2] is the talker ("GP", "GL", "GA", "BD"/"GB", "GQ", ...).
    if (!strncmp(sentence + 1, "GP", 2)) return GPS_CONST_GPS;
    if (!strncmp(sentence + 1, "GL", 2)) return GPS_CONST_GLONASS;
    if (!strncmp(sentence + 1, "GA", 2)) return GPS_CONST_GALILEO;
    if (!strncmp(sentence + 1, "BD", 2)) return GPS_CONST_BEIDOU;
    if (!strncmp(sentence + 1, "GB", 2)) return GPS_CONST_BEIDOU;  // some receivers use GB instead of BD
    if (!strncmp(sentence + 1, "GQ", 2)) return GPS_CONST_QZSS;
    return GPS_CONST_UNKNOWN;
}

static gps_constellation_t system_id_to_constellation(int sys_id)
{
    switch (sys_id) {
    case 1: return GPS_CONST_GPS;
    case 2: return GPS_CONST_GLONASS;
    case 3: return GPS_CONST_GALILEO;
    case 4: return GPS_CONST_BEIDOU;
    case 5: return GPS_CONST_QZSS;
    default: return GPS_CONST_UNKNOWN;
    }
}

// Latest $GxGSA result per constellation -- kept separate from
// s_state.satellites[] rather than folded straight in, because GSV fully
// replaces a constellation's satellite entries each cycle (see handle_gsv())
// and would otherwise wipe out a used_in_solution flag set by a GSA that
// arrived earlier in the same cycle. Both handlers re-apply this table to
// s_state.satellites[] after touching it, so either arrival order ends up
// consistent within one cycle.
static uint8_t s_used_prns[GPS_CONST_COUNT][12];
static int     s_used_count[GPS_CONST_COUNT];

// Re-marks used_in_solution on every satellite of one constellation already
// in s_state, from s_used_prns[c]. Caller holds s_mutex.
static void reapply_used_flags_locked(gps_constellation_t c)
{
    for (int i = 0; i < s_state.satellite_count; i++) {
        gps_satellite_t *sat = &s_state.satellites[i];
        if (sat->constellation != c) continue;
        sat->used_in_solution = false;
        for (int j = 0; j < s_used_count[c]; j++) {
            if (s_used_prns[c][j] == sat->prn) {
                sat->used_in_solution = true;
                break;
            }
        }
    }
}

static void handle_gsa(char *sentence)
{
    gps_constellation_t c = talker_to_constellation(sentence); // usually GPS_CONST_UNKNOWN -- this module's GSA talker is always "GN"

    char *cursor = sentence;
    (void)next_field(&cursor);  // $GxGSA
    (void)next_field(&cursor);  // mode1 (M/A) -- not used
    char *fix_type_s = next_field(&cursor);  // fix type: 1=no fix, 2=2D, 3=3D

    uint8_t prns[12];
    int n = 0;
    for (int i = 0; i < 12; i++) {
        char *f = next_field(&cursor);
        if (f && f[0] != '\0') prns[n++] = (uint8_t)atoi(f);
    }
    char *pdop_s = next_field(&cursor);
    (void)next_field(&cursor);  // HDOP -- GGA's own HDOP field is what feeds the rest of the app already
    char *vdop_s = next_field(&cursor);
    char *sys_id = next_field(&cursor);  // trailing system ID -- see file header comment
    if (sys_id && sys_id[0] != '\0') {
        c = system_id_to_constellation(atoi(sys_id));
    }
    if (c == GPS_CONST_UNKNOWN) return;  // can't tell which constellation this is for

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_used_count[c] = n;
    memcpy(s_used_prns[c], prns, (size_t)n);
    reapply_used_flags_locked(c);
    // This module's GSA talker is always "GN" (see this function's own
    // comment above) -- a single combined solution, not per-constellation,
    // so DOP/fix type are plain s_state fields rather than living in the
    // s_used_count[]-style per-constellation arrays above.
    if (fix_type_s && fix_type_s[0] != '\0') {
        s_state.fix_type = atoi(fix_type_s);
    }
    if (pdop_s && pdop_s[0] != '\0' && vdop_s && vdop_s[0] != '\0') {
        s_state.pdop = strtof(pdop_s, NULL);
        s_state.vdop = strtof(vdop_s, NULL);
        s_state.dop_valid = true;
    }
    xSemaphoreGive(s_mutex);
}

// Multi-part accumulation scratch for the constellation currently being
// assembled -- one shared buffer, not one per constellation, since this
// module's GSV sentences for a given constellation arrive as a clean
// consecutive run (confirmed empirically, see file header comment); if a
// future module interleaves them instead, a message landing out of
// sequence just gets dropped by the "num == 1 resets, others append"
// logic below rather than corrupting another constellation's data.
#define GSV_SCRATCH_MAX 16  // generous headroom for one constellation's satellites in view
static gps_satellite_t s_gsv_scratch[GSV_SCRATCH_MAX];
static int s_gsv_scratch_count;
static gps_constellation_t s_gsv_scratch_const = GPS_CONST_UNKNOWN;

static void handle_gsv(char *sentence)
{
    gps_constellation_t c = talker_to_constellation(sentence);
    if (c == GPS_CONST_UNKNOWN) return;

    char *cursor = sentence;
    (void)next_field(&cursor);  // $GxGSV
    char *total_s = next_field(&cursor);
    char *num_s   = next_field(&cursor);
    (void)next_field(&cursor);  // sats-in-view total -- s_gsv_scratch_count ends up being this anyway
    if (!total_s || !num_s) return;
    int total = atoi(total_s);
    int num   = atoi(num_s);
    if (num < 1 || total < 1) return;

    if (num == 1) {
        s_gsv_scratch_count = 0;
        s_gsv_scratch_const = c;
    } else if (c != s_gsv_scratch_const) {
        // Out-of-sequence relative to what we started accumulating -- drop
        // it rather than mix constellations (see this block's header comment).
        return;
    }

    for (int i = 0; i < 4 && s_gsv_scratch_count < GSV_SCRATCH_MAX; i++) {
        char *prn  = next_field(&cursor);
        char *elev = next_field(&cursor);
        char *azim = next_field(&cursor);
        char *snr  = next_field(&cursor);
        if (!prn || prn[0] == '\0') break;  // this sentence has fewer than 4 (last one in the group usually does)

        gps_satellite_t *sat = &s_gsv_scratch[s_gsv_scratch_count++];
        memset(sat, 0, sizeof(*sat));
        sat->prn = (uint8_t)atoi(prn);
        sat->elevation_deg = (elev && elev[0]) ? (uint8_t)atoi(elev) : 0;
        sat->azimuth_deg = (azim && azim[0]) ? (uint16_t)atoi(azim) : 0;
        sat->has_snr = snr && snr[0] != '\0';
        sat->snr = sat->has_snr ? (uint8_t)atoi(snr) : 0;
        sat->constellation = c;
        // signal ID (last field, before checksum) not parsed -- this app
        // doesn't distinguish L1 vs L5 etc. per satellite, just whether
        // it's tracked at all.
    }

    if (num < total) return;  // more sentences still coming for this constellation

    // Full group assembled -- replace this constellation's entries in
    // s_state.satellites[] wholesale (satellites that dropped out of view
    // just aren't in the new list, same as the module itself stops
    // reporting them rather than flagging them "gone" some other way).
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    gps_satellite_t merged[GPS_MAX_SATS];
    int merged_count = 0;
    for (int i = 0; i < s_state.satellite_count && merged_count < GPS_MAX_SATS; i++) {
        if (s_state.satellites[i].constellation != c) {
            merged[merged_count++] = s_state.satellites[i];
        }
    }
    for (int i = 0; i < s_gsv_scratch_count && merged_count < GPS_MAX_SATS; i++) {
        merged[merged_count++] = s_gsv_scratch[i];
    }
    memcpy(s_state.satellites, merged, sizeof(gps_satellite_t) * (size_t)merged_count);
    s_state.satellite_count = merged_count;
    reapply_used_flags_locked(c);  // in case this constellation's GSA already arrived this cycle
    xSemaphoreGive(s_mutex);
}

// ---------------------------------------------------------------------------
// Sentence handlers (operate on a local copy, then lock to update state)
// ---------------------------------------------------------------------------

static void handle_gga(char *sentence)
{
    char *cursor = sentence;
    (void)next_field(&cursor);         // $GxGGA
    char *utc  = next_field(&cursor);
    char *lat  = next_field(&cursor);
    char *ns   = next_field(&cursor);
    char *lon  = next_field(&cursor);
    char *ew   = next_field(&cursor);
    char *fix  = next_field(&cursor);
    char *sats = next_field(&cursor);
    char *hdop = next_field(&cursor);
    char *alt  = next_field(&cursor);
    char *alt_units = next_field(&cursor);   // always "M" (meters) per NMEA 0183

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
    if (hdop && hdop[0] != '\0') {
        tmp.hdop = strtof(hdop, NULL);
        tmp.hdop_valid = true;
    }
    // alt_units is always "M" per spec -- not checked, just consumed so a
    // future field doesn't shift out from under next_field() by accident.
    (void)alt_units;
    if (alt && alt[0] != '\0') {
        tmp.altitude_m = strtof(alt, NULL);
        tmp.altitude_valid = true;
    }

    // Settings screen's "Update rate" -- measured from real GGA-to-GGA
    // timing, not read from the module's configuration (it has no command
    // interface here, see this file's header). Bounds reject a stray huge
    // gap (e.g. this task just started) or implausibly short one (line
    // noise producing back-to-back sentences) rather than showing a wild
    // number for one tick; light EMA smooths ordinary jitter, same
    // 0.8/0.2 split gps_ui_bridge.c's vertical-speed smoothing uses.
    int64_t now_us = esp_timer_get_time();
    if (s_last_gga_us != 0) {
        float interval_s = (float)(now_us - s_last_gga_us) / 1e6f;
        if (interval_s > 0.05f && interval_s < 10.0f) {
            float measured_hz = 1.0f / interval_s;
            tmp.fix_rate_hz = tmp.fix_rate_valid
                ? (0.8f * tmp.fix_rate_hz + 0.2f * measured_hz) : measured_hz;
            tmp.fix_rate_valid = true;
        }
    }
    s_last_gga_us = now_us;

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = tmp;
    xSemaphoreGive(s_mutex);

    ESP_LOGI(TAG, "GGA fix=%d sats=%d lat=%.5f lon=%.5f hdop=%.1f alt=%.1fm",
             tmp.gga_fix, tmp.sats_in_use, tmp.latitude_deg, tmp.longitude_deg,
             tmp.hdop, tmp.altitude_m);
}

static void handle_rmc(char *sentence)
{
    char *cursor = sentence;
    (void)next_field(&cursor);         // $GxRMC
    char *utc    = next_field(&cursor);
    char *status = next_field(&cursor);
    char *lat    = next_field(&cursor);
    char *ns     = next_field(&cursor);
    char *lon    = next_field(&cursor);
    char *ew     = next_field(&cursor);
    char *speed  = next_field(&cursor);
    char *track  = next_field(&cursor);
    char *date   = next_field(&cursor);

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
    } else if (!strncmp(sentence + 3, "GSV", 3)) {
        handle_gsv(sentence);
    } else if (!strncmp(sentence + 3, "GSA", 3)) {
        handle_gsa(sentence);
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
                    if (s_log_f) {
                        bool ok = fprintf(s_log_f, "%lld %s\n",
                                          (long long)(esp_timer_get_time() / 1000), line) >= 0;
                        // fflush() only pushes data into FatFs's write path (f_write) --
                        // the directory entry's file-size field isn't committed to the
                        // card until f_sync()/f_close(). This device gets no clean
                        // shutdown (battery just dies outdoors), so without an explicit
                        // fsync() here, a sudden power loss can leave the file's data
                        // clusters written but its on-disk length still 0 -- confirmed
                        // on real hardware: a full outdoor logging session round-tripped
                        // as an empty file until this was added.
                        ok = ok && (fflush(s_log_f) == 0);
                        ok = ok && (fsync(fileno(s_log_f)) == 0);
                        s_log_ok = ok;  // drives the SD-status icon in ui_overlay.c
                    }
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

    if (sd_card_is_mounted()) {
        s_log_f = fopen(GPS_LOG_PATH, "a");  // append -- accumulate across boots
        if (s_log_f) {
            bool ok = fprintf(s_log_f, "--- boot, uptime_ms t0 ---\n") >= 0;
            ok = ok && (fflush(s_log_f) == 0);
            ok = ok && (fsync(fileno(s_log_f)) == 0);
            s_log_ok = ok;
            ESP_LOGI(TAG, "logging raw NMEA to %s", GPS_LOG_PATH);
        } else {
            ESP_LOGW(TAG, "could not open %s for logging", GPS_LOG_PATH);
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted -- no NMEA log this session");
    }

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

bool gps_log_active(void)
{
    return s_log_f != NULL && s_log_ok;
}
