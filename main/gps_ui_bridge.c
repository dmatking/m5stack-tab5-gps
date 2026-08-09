// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Translates main/gps.c's gps_state_t into the design UI's setter calls
// (main/ui_home.h/ui_common.h). Runs on its own task since gps.c's reader
// task and this one are independent -- polls gps_get_state() rather than
// being driven by it, same snapshot-read pattern main/map_view.c already
// uses for its own GPS-derived features (initial centering, follow mode).

#include "gps_ui_bridge.h"

#include <math.h>
#include <stdio.h>

#include "design_ui.h"
#include "gps.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GPS_UI_BRIDGE";

#define TICK_PERIOD_MS 500

// "32° 54.1234' N" / "097° 19.5678' W" -- latitude degrees are 2 digits
// (max 90), longitude 3 (max 180), matching the mockup's own formatting.
static void format_ddm(double decimal_deg, bool is_lat, char *out, size_t out_size)
{
    double a = fabs(decimal_deg);
    int deg = (int)a;
    double min = (a - deg) * 60.0;
    char hemi = is_lat ? (decimal_deg >= 0 ? 'N' : 'S')
                        : (decimal_deg >= 0 ? 'E' : 'W');
    if (is_lat) {
        snprintf(out, out_size, "%d\xC2\xB0 %07.4f' %c", deg, min, hemi);
    } else {
        snprintf(out, out_size, "%03d\xC2\xB0 %07.4f' %c", deg, min, hemi);
    }
}

// 8-point compass -- "NE"/"SW" etc. to match the mockup, not 16-point.
static const char *cardinal_8(float deg)
{
    static const char *names[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int idx = ((int)((deg + 22.5f) / 45.0f)) & 7;
    return names[idx];
}

// HDOP -> a rough horizontal-accuracy estimate. Not a real CEP/precision
// figure -- just HDOP times a typical civilian-GPS user-range-error
// constant (~5m), which is the standard back-of-envelope approximation
// when the receiver doesn't report its own accuracy estimate directly.
static float hdop_to_accuracy_ft(float hdop)
{
    const float uere_m = 5.0f;
    return hdop * uere_m * 3.28084f;
}

// Not relying on M_PI from math.h -- see the identical comment/pattern in
// main/map_view.c's MAP_PI, same reasoning (avoid a feature-test-macro
// dependency for one constant used in exactly one place here too).
static const double GPS_UI_PI = 3.14159265358979323846;

// Great-circle distance between two WGS84 points, in miles. Used by the
// Telemetry trip accumulator below -- nothing else in this file needs it.
static float haversine_miles(double lat1, double lon1, double lat2, double lon2)
{
    const double r_mi = 3958.8;
    double phi1 = lat1 * (GPS_UI_PI / 180.0);
    double phi2 = lat2 * (GPS_UI_PI / 180.0);
    double dphi = (lat2 - lat1) * (GPS_UI_PI / 180.0);
    double dlambda = (lon2 - lon1) * (GPS_UI_PI / 180.0);
    double a = sin(dphi / 2.0) * sin(dphi / 2.0)
             + cos(phi1) * cos(phi2) * sin(dlambda / 2.0) * sin(dlambda / 2.0);
    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
    return (float)(r_mi * c);
}

// Telemetry's trip/max-speed/moving-time accumulator. Session-only -- there's
// no reset button anywhere in the UI yet, so these climb from zero at boot
// and keep going for as long as the app runs. Distance/moving-time only
// accrue above MOVING_MPH_THRESHOLD, to keep GPS position jitter while
// parked from slowly inflating the trip odometer.
#define MOVING_MPH_THRESHOLD 1.15f  // ~1 knot

static bool   s_have_prev_pos;
static double s_prev_lat, s_prev_lon;
static float  s_trip_miles;
static float  s_max_mph;
static uint32_t s_moving_ticks;

// Vertical speed (fpm) from consecutive altitude readings, 500ms apart --
// noisy on its own (GPS altitude jitter amplified ~120x by the short
// interval), so smoothed with a simple exponential moving average rather
// than shown raw.
static bool  s_have_prev_alt;
static float s_prev_alt_ft;
static float s_vspeed_fpm_ema;

static void tick(void)
{
    gps_state_t st = gps_get_state();
    bool fix = gps_has_fix();

    if (!lvgl_port_lock(50)) return;

    ui_home_t *h = ui_home();
    if (!h) {
        lvgl_port_unlock();
        return;
    }

    ui_status_set_fix(&h->status, fix ? "GPS FIX" : "NO FIX", fix);
    ui_status_set_sats(&h->status, st.sats_in_use, st.hdop_valid ? st.hdop : 0.0f);

    // No RTC/timezone handling in this project -- shown as UTC, not local
    // time, even though the mockup's status-bar clock implies local ("10:24
    // AM"). Real UTC beats a frozen demo string more than it beats being
    // technically mislabeled.
    if (st.time_valid) {
        char clock_buf[16];
        snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d UTC", st.utc_tm.tm_hour, st.utc_tm.tm_min);
        ui_status_set_clock(&h->status, clock_buf);
    }

    if (st.latlon_valid) {
        char lat_buf[32], lon_buf[32];
        format_ddm(st.latitude_deg, true, lat_buf, sizeof(lat_buf));
        format_ddm(st.longitude_deg, false, lon_buf, sizeof(lon_buf));
        ui_home_set_position(h, lat_buf, lon_buf);
    }

    if (st.speed_valid) {
        ui_home_set_speed(h, st.speed_knots * 1.15078f);
    }

    // heading_deg comes from RMC and is only meaningful in motion -- GPS
    // course-over-ground is noisy/undefined near zero speed. Shown
    // regardless for now (matches gps.c not gating it either); revisit if
    // it looks jittery standing still once there's a real heading to look at.
    ui_home_set_heading(h, (int)(st.heading_deg + 0.5f), cardinal_8(st.heading_deg));

    if (st.altitude_valid) {
        ui_home_set_altitude(h, (int)(st.altitude_m * 3.28084f + 0.5f));
    }

    if (st.hdop_valid) {
        ui_home_set_accuracy(h, hdop_to_accuracy_ft(st.hdop));
    }

    const char *sat_quality = st.sats_in_use >= 7 ? "Good"
                             : st.sats_in_use >= 4 ? "Fair"
                                                   : "Poor";
    ui_home_set_satellites(h, st.sats_in_use, sat_quality);

    if (st.time_valid || st.date_valid) {
        char hms_buf[16] = {0};
        char date_buf[32] = {0};
        static const char *months[12] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
        };
        if (st.time_valid) {
            snprintf(hms_buf, sizeof(hms_buf), "%02d:%02d:%02d",
                     st.utc_tm.tm_hour, st.utc_tm.tm_min, st.utc_tm.tm_sec);
        }
        if (st.date_valid && st.utc_tm.tm_mon >= 0 && st.utc_tm.tm_mon < 12) {
            snprintf(date_buf, sizeof(date_buf), "%s %d, %d",
                     months[st.utc_tm.tm_mon], st.utc_tm.tm_mday, st.utc_tm.tm_year + 1900);
        }
        ui_home_set_utc(h, st.time_valid ? hms_buf : NULL, st.date_valid ? date_buf : NULL);
    }

    // Deliberately not touched: Home's own trip widget (needs avg-speed and
    // elevation-gain too, which the accumulator below doesn't compute -- see
    // gps_ui_bridge.h) and battery percent (no fuel-gauge hardware wired up
    // yet). Both stay at their ui_home_create() demo values rather than
    // being fed something fake.

    // ---- Telemetry screen -------------------------------------------------
    // Same live gps_get_state() snapshot as Home, above -- reuses the speed/
    // heading/ddm values already computed there instead of recomputing them.
    ui_telemetry_t *t = ui_telemetry();
    if (t) {
        ui_status_set_fix(&t->status, fix ? "GPS FIX" : "NO FIX", fix);
        ui_status_set_sats(&t->status, st.sats_in_use, st.hdop_valid ? st.hdop : 0.0f);
        if (st.time_valid) {
            char clock_buf[16];
            snprintf(clock_buf, sizeof(clock_buf), "%02d:%02d UTC", st.utc_tm.tm_hour, st.utc_tm.tm_min);
            ui_status_set_clock(&t->status, clock_buf);
        }

        float speed_mph = 0.0f;
        bool moving_now = false;
        if (st.speed_valid) {
            speed_mph = st.speed_knots * 1.15078f;
            ui_telemetry_set_speed(t, speed_mph);
            moving_now = speed_mph > MOVING_MPH_THRESHOLD;
            if (speed_mph > s_max_mph) s_max_mph = speed_mph;
            if (moving_now) s_moving_ticks++;
        }

        ui_telemetry_set_heading(t, (int)(st.heading_deg + 0.5f));

        if (st.latlon_valid) {
            char lat_buf[32], lon_buf[32];
            format_ddm(st.latitude_deg, true, lat_buf, sizeof(lat_buf));
            format_ddm(st.longitude_deg, false, lon_buf, sizeof(lon_buf));
            ui_telemetry_set_position(t, lat_buf, lon_buf, st.latitude_deg, st.longitude_deg);

            // Only add to the trip odometer while actually moving (per
            // MOVING_MPH_THRESHOLD above) -- otherwise parked GPS jitter
            // between fixes slowly accumulates fake distance.
            if (s_have_prev_pos && moving_now) {
                s_trip_miles += haversine_miles(s_prev_lat, s_prev_lon,
                                                st.latitude_deg, st.longitude_deg);
            }
            s_prev_lat = st.latitude_deg;
            s_prev_lon = st.longitude_deg;
            s_have_prev_pos = true;
        }

        if (st.altitude_valid) {
            float alt_ft = st.altitude_m * 3.28084f;
            float vspeed_fpm = 0.0f;
            if (s_have_prev_alt) {
                // TICK_PERIOD_MS apart -> scale a per-tick delta up to
                // feet-per-minute, then smooth (see s_vspeed_fpm_ema's
                // declaration for why raw deltas are too noisy to show).
                float raw_fpm = (alt_ft - s_prev_alt_ft) * (60000.0f / TICK_PERIOD_MS);
                s_vspeed_fpm_ema = 0.8f * s_vspeed_fpm_ema + 0.2f * raw_fpm;
                vspeed_fpm = s_vspeed_fpm_ema;
            }
            s_prev_alt_ft = alt_ft;
            s_have_prev_alt = true;
            ui_telemetry_set_altitude(t, (int)(alt_ft + 0.5f), (int)(vspeed_fpm + 0.5f));
        }

        // "visible" is really just sats_in_use again -- GGA only reports
        // satellites *used* in the fix, not the full visible constellation,
        // and gps.c doesn't parse GSV (the sentence that would give per-
        // satellite visibility/SNR) yet. Real used-count beats a frozen
        // demo "visible" number.
        if (st.hdop_valid) {
            ui_telemetry_set_quality(t, st.sats_in_use, st.sats_in_use,
                                     st.hdop, hdop_to_accuracy_ft(st.hdop));
        }

        // Same UTC-not-local tradeoff as Home's clock, above -- feeding the
        // real UTC time into both fields rather than mislabeling it, or
        // showing a frozen "10:24 AM local" demo string. Revisit if this
        // project ever gets a timezone setting.
        if (st.time_valid) {
            char hms_buf[16];
            snprintf(hms_buf, sizeof(hms_buf), "%02d:%02d:%02d",
                     st.utc_tm.tm_hour, st.utc_tm.tm_min, st.utc_tm.tm_sec);
            ui_telemetry_set_time(t, hms_buf, hms_buf);
        }

        uint32_t moving_s = (uint32_t)(s_moving_ticks * (TICK_PERIOD_MS / 1000.0f));
        char moving_buf[16];
        snprintf(moving_buf, sizeof(moving_buf), "%u:%02u",
                 (unsigned)(moving_s / 3600), (unsigned)((moving_s % 3600) / 60));
        ui_telemetry_set_trip(t, s_trip_miles, s_max_mph, moving_buf);

        // Signal bars / constellation list stay at ui_telemetry_create()'s
        // demo values -- would need GSV parsing (per-satellite SNR) that
        // gps.c doesn't do yet, same limitation as the "visible" count above.
    }

    lvgl_port_unlock();
}

static void bridge_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "started, %d ms tick", TICK_PERIOD_MS);
    while (1) {
        tick();
        vTaskDelay(pdMS_TO_TICKS(TICK_PERIOD_MS));
    }
}

void gps_ui_bridge_start(void)
{
    xTaskCreate(bridge_task, "gps_ui_bridge", 4096, NULL, 3, NULL);
}
