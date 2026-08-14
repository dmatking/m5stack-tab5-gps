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

#include "app_settings.h"
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

// ---- US Central time (America/Chicago) --------------------------------
// This project's users are all in one place (Fort Worth, TX), so a real
// IANA tzdata dependency for one hardcoded timezone would be a lot of
// weight for not much -- these are the actual current US DST rules
// (Energy Policy Act of 2005, in effect since 2007): starts 2nd Sunday of
// March at 2:00 AM standard time (08:00 UTC), ends 1st Sunday of November
// at 2:00 AM daylight time (07:00 UTC). Not relying on newlib's
// timegm()/mktime() either -- worked out by hand instead, using Howard
// Hinnant's well-known days_from_civil()/civil_from_days() algorithms
// (public domain, widely used in chrono-compatible libraries). Verified
// against Python's datetime/zoneinfo before trusting this on real
// hardware: a 2000-case days_from_civil<->civil_from_days round-trip, the
// 2nd-Sunday-of-March/1st-Sunday-of-November transition day for 8 separate
// years, and the exact "spring forward" 2am->3am skip / "fall back"
// 1am-repeats-twice instants -- all matched.

// Days since 1970-01-01 for a proleptic-Gregorian civil date.
static long days_from_civil(int y, int m, int d)
{
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (unsigned)(m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468L;
}

// Inverse of days_from_civil() -- also Hinnant's algorithm.
static void civil_from_days(long z, int *out_y, int *out_m, int *out_d)
{
    z += 719468L;
    long era = (z >= 0 ? z : z - 146096L) / 146097L;
    unsigned long doe = (unsigned long)(z - era * 146097L);
    unsigned long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned long mp = (5 * doy + 2) / 153;
    unsigned long d = doy - (153 * mp + 2) / 5 + 1;
    unsigned long m = mp + (mp < 10 ? 3 : (unsigned long)-9);
    y += (m <= 2);
    *out_y = (int)y;
    *out_m = (int)m;
    *out_d = (int)d;
}

// Day-of-month (1-31) of the nth (1-based) Sunday in the given month/year.
static int nth_sunday_of_month(int year, int month, int nth)
{
    long d1 = days_from_civil(year, month, 1);
    // 1970-01-01 was a Thursday; this remaps to Sunday=0..Saturday=6.
    int wd1 = (int)(((d1 % 7) + 11) % 7);
    int first_sunday = 1 + (7 - wd1) % 7;
    return first_sunday + 7 * (nth - 1);
}

// True if the given UTC instant falls within US Central daylight time.
static bool us_central_is_dst(int year, int month, int day, int hour)
{
    if (month > 3 && month < 11) return true;
    if (month < 3 || month > 11) return false;
    if (month == 3) {
        int start_day = nth_sunday_of_month(year, 3, 2);
        return day > start_day || (day == start_day && hour >= 8);
    }
    int end_day = nth_sunday_of_month(year, 11, 1);
    return day < end_day || (day == end_day && hour < 7);
}

// Converts a UTC broken-down time to US Central, handling date rollover
// (midnight-crossing, and the DST-transition hour skip/repeat) correctly --
// not just a naive hour subtraction. out_abbrev is always a static string
// literal ("CDT"/"CST"), safe for the caller to just hold onto the pointer.
static void us_central_from_utc(const struct tm *utc, struct tm *out_local,
                                 const char **out_abbrev)
{
    int year = utc->tm_year + 1900, month = utc->tm_mon + 1, day = utc->tm_mday;
    bool dst = us_central_is_dst(year, month, day, utc->tm_hour);
    int offset_hours = dst ? -5 : -6;
    *out_abbrev = dst ? "CDT" : "CST";

    long days = days_from_civil(year, month, day);
    long total_s = days * 86400L + utc->tm_hour * 3600L + utc->tm_min * 60L + utc->tm_sec;
    total_s += (long)offset_hours * 3600L;

    long local_days = total_s / 86400L;
    long rem = total_s % 86400L;
    if (rem < 0) { rem += 86400L; local_days -= 1; }

    out_local->tm_hour = (int)(rem / 3600L);
    out_local->tm_min  = (int)((rem % 3600L) / 60L);
    out_local->tm_sec  = (int)(rem % 60L);

    int ly, lm, ld;
    civil_from_days(local_days, &ly, &lm, &ld);
    out_local->tm_year = ly - 1900;
    out_local->tm_mon  = lm - 1;
    out_local->tm_mday = ld;
}

// Formats hour/min[/sec] as either 24-hour ("15:24"/"15:24:18") or
// 12-hour ("3:24 PM"/"3:24:18 PM") depending on the Settings screen's
// "24-hour time" switch (main/app_settings.h) -- shared by every clock
// this file drives (Home/Telemetry's status-bar clocks and big time
// cards, Telemetry's separate UTC card), so flipping the setting changes
// all of them together rather than some ad hoc subset.
static void format_clock(char *out, size_t out_size, int hour24, int min, int sec, bool with_seconds)
{
    if (app_settings_get_time_24h()) {
        if (with_seconds) snprintf(out, out_size, "%02d:%02d:%02d", hour24, min, sec);
        else              snprintf(out, out_size, "%02d:%02d", hour24, min);
        return;
    }
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    const char *ampm = hour24 < 12 ? "AM" : "PM";
    if (with_seconds) snprintf(out, out_size, "%d:%02d:%02d %s", hour12, min, sec, ampm);
    else              snprintf(out, out_size, "%d:%02d %s", hour12, min, ampm);
}

// Same idea as format_clock(), but for Home's big time card specifically,
// which needs the AM/PM suffix on its own (smaller) line rather than
// appended inline -- "3:24:18 PM" was wide enough to clip against that
// card's edges in 12-hour mode. ampm_out is "" in 24-hour mode (nothing to
// show); ui_home_set_local_time() hides that line entirely when it sees
// an empty string, rather than leaving a gap.
static void format_clock_split(char *hms_out, size_t hms_size,
                                char *ampm_out, size_t ampm_size,
                                int hour24, int min, int sec)
{
    if (app_settings_get_time_24h()) {
        snprintf(hms_out, hms_size, "%02d:%02d:%02d", hour24, min, sec);
        ampm_out[0] = '\0';
        return;
    }
    int hour12 = hour24 % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(hms_out, hms_size, "%d:%02d:%02d", hour12, min, sec);
    snprintf(ampm_out, ampm_size, "%s", hour24 < 12 ? "AM" : "PM");
}

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

// Telemetry's (and now Home's, see ui_home_set_trip() below) trip/max-speed/
// moving-time/elevation-gain accumulator. Session-only until
// gps_ui_bridge_reset_trip() is called (wired to Home's Reset Trip button,
// see ui_home_set_reset_trip_cb()) -- otherwise these just climb from zero
// at boot for as long as the app runs. Distance/moving-time/elevation-gain
// only accrue above MOVING_MPH_THRESHOLD, to keep GPS position/altitude
// jitter while parked from slowly inflating the trip odometer.
#define MOVING_MPH_THRESHOLD 1.15f  // ~1 knot

static bool   s_have_prev_pos;
static double s_prev_lat, s_prev_lon;
static float  s_trip_miles;
static float  s_max_mph;
static uint32_t s_moving_ticks;
static float  s_elev_gain_ft;

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

    // US Central time, not UTC -- this project's users are all in one
    // place, so showing raw UTC (technically simpler, but requires doing
    // timezone arithmetic in your head every time you glance at the clock)
    // lost out to showing the real local time. Needs both time AND date
    // (to know which DST rule applies and to handle midnight rollover),
    // so this waits for both rather than showing anything UTC-based as a
    // fallback -- see us_central_from_utc() above.
    bool have_local = st.time_valid && st.date_valid;
    struct tm local_tm;
    const char *tz_abbrev = NULL;
    if (have_local) {
        us_central_from_utc(&st.utc_tm, &local_tm, &tz_abbrev);
        char time_part[16], clock_buf[24];
        format_clock(time_part, sizeof(time_part), local_tm.tm_hour, local_tm.tm_min, 0, false);
        snprintf(clock_buf, sizeof(clock_buf), "%s %s", time_part, tz_abbrev);
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

    if (have_local) {
        static const char *months[12] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
        };
        char hms_buf[16], ampm_buf[4], date_buf[32];
        format_clock_split(hms_buf, sizeof(hms_buf), ampm_buf, sizeof(ampm_buf),
                           local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
        snprintf(date_buf, sizeof(date_buf), "%s %d, %d",
                 months[local_tm.tm_mon], local_tm.tm_mday, local_tm.tm_year + 1900);
        ui_home_set_local_time(h, hms_buf, ampm_buf, date_buf, tz_abbrev);
    }

    // Home's trip widget is set further below, alongside Telemetry's -- both
    // read the same accumulator, computed there. Battery percent deliberately
    // NOT touched: no fuel-gauge hardware wired up yet, stays at
    // ui_home_create()'s demo value rather than being fed something fake.

    // ---- Telemetry screen -------------------------------------------------
    // Trimmed down to only what Home's own cards don't already show (see
    // ui_telemetry_create()'s comment) -- speed/heading/DDM position/trip
    // are still computed below where something else still needs them
    // (the trip accumulator, Home's own cards), just no longer also pushed
    // into Telemetry setters that no longer exist.
    ui_telemetry_t *t = ui_telemetry();
    if (t) {
        ui_status_set_fix(&t->status, fix ? "GPS FIX" : "NO FIX", fix);
        ui_status_set_sats(&t->status, st.sats_in_use, st.hdop_valid ? st.hdop : 0.0f);
        if (have_local) {
            char time_part[16], clock_buf[24];
            format_clock(time_part, sizeof(time_part), local_tm.tm_hour, local_tm.tm_min, 0, false);
            snprintf(clock_buf, sizeof(clock_buf), "%s %s", time_part, tz_abbrev);
            ui_status_set_clock(&t->status, clock_buf);
        }

        float speed_mph = 0.0f;
        bool moving_now = false;
        if (st.speed_valid) {
            speed_mph = st.speed_knots * 1.15078f;
            moving_now = speed_mph > MOVING_MPH_THRESHOLD;
            if (speed_mph > s_max_mph) s_max_mph = speed_mph;
            if (moving_now) s_moving_ticks++;
        }

        if (st.latlon_valid) {
            ui_telemetry_set_position(t, st.latitude_deg, st.longitude_deg);

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

                // Elevation gain: same moving-gated raw-delta approach as
                // the trip odometer above, for the same reason (parked GPS
                // altitude jitter shouldn't slowly inflate "gain" out of
                // nothing). Uses the raw per-tick delta, not the smoothed
                // EMA -- matches the odometer's own precedent of not
                // smoothing, though unlike distance this really is just
                // summing noisy jitter while moving, so treat it as a
                // rough total, not a precise one.
                if (moving_now) {
                    float delta_ft = alt_ft - s_prev_alt_ft;
                    if (delta_ft > 0.0f) s_elev_gain_ft += delta_ft;
                }
            }
            s_prev_alt_ft = alt_ft;
            s_have_prev_alt = true;
            ui_telemetry_set_vspeed(t, (int)(vspeed_fpm + 0.5f));
        }

        if (st.hdop_valid) {
            ui_telemetry_set_hdop(t, st.hdop);
        }

        // Unlike Home's single (now Central) clock, Telemetry has room for
        // both a real LOCAL (Central) and a real UTC card side by side --
        // no UTC-vs-local tradeoff to make here, just show both for real.
        if (have_local) {
            char local_buf[16], utc_buf[16];
            format_clock(local_buf, sizeof(local_buf), local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, true);
            format_clock(utc_buf, sizeof(utc_buf), st.utc_tm.tm_hour, st.utc_tm.tm_min, st.utc_tm.tm_sec, true);
            ui_telemetry_set_time(t, local_buf, utc_buf, tz_abbrev);
        }

        // Feeds Home's trip card only now -- Telemetry's own TRIP/MAX SPEED/
        // MOVING row is gone (see ui_telemetry_create()'s comment), this
        // was its last use on this screen.
        uint32_t moving_s = (uint32_t)(s_moving_ticks * (TICK_PERIOD_MS / 1000.0f));
        char moving_hms_buf[16];
        snprintf(moving_hms_buf, sizeof(moving_hms_buf), "%u:%02u:%02u",
                 (unsigned)(moving_s / 3600), (unsigned)((moving_s % 3600) / 60),
                 (unsigned)(moving_s % 60));
        float avg_mph = 0.0f;
        if (s_moving_ticks > 0) {
            float moving_hours = s_moving_ticks * (TICK_PERIOD_MS / 1000.0f) / 3600.0f;
            avg_mph = s_trip_miles / moving_hours;
        }
        ui_home_set_trip(h, s_trip_miles, moving_hms_buf, avg_mph, s_max_mph,
                         (int)(s_elev_gain_ft + 0.5f));

        // Signal bars -- one per satellite gps.c's GSV/GSA parsing currently
        // has in view, real SNR/constellation/used-in-solution per bar. See
        // gps.h's gps_satellite_t; ui_telemetry_set_signal() takes plain
        // arrays rather than that struct directly to keep this screen's
        // header decoupled from gps.h's types (same reasoning as its other
        // setters).
        // "#RRGGBB text#" recolor spans (see ui_telemetry_create()'s
        // lv_label_set_recolor()) so each name matches its own bars below --
        // hex values must match ui_theme.h's UI_C_*_HEX (bright variants).
        static const char *const_names[5] = {
            "#" UI_C_GPS_HEX     " GPS#",
            "#" UI_C_GLONASS_HEX " GLONASS#",
            "#" UI_C_GALILEO_HEX " GALILEO#",
            "#" UI_C_BEIDOU_HEX  " BEIDOU#",
            "#" UI_C_QZSS_HEX    " QZSS#",
        };
        uint8_t sig_snr[UI_TELEM_BARS];
        bool    sig_has_snr[UI_TELEM_BARS];
        uint8_t sig_const[UI_TELEM_BARS];
        bool    sig_used[UI_TELEM_BARS];
        bool    const_seen[5] = { false, false, false, false, false };
        int used_count = 0;
        int n = st.satellite_count;
        for (int i = 0; i < n; i++) {
            const gps_satellite_t *sat = &st.satellites[i];
            if (sat->used_in_solution) used_count++;
            if (sat->constellation < 5) const_seen[sat->constellation] = true;
            if (i < UI_TELEM_BARS) {
                sig_snr[i]      = sat->snr;
                sig_has_snr[i]  = sat->has_snr;
                sig_const[i]    = (uint8_t)sat->constellation;
                sig_used[i]     = sat->used_in_solution;
            }
        }
        if (n > UI_TELEM_BARS) n = UI_TELEM_BARS;

        // snprintf-with-running-offset rather than strlcat() -- this
        // codebase already avoids libc's BSD string extensions elsewhere
        // (see ui_goto.c's lv_strlcpy() use instead of plain strlcpy()).
        // 128 bytes comfortably covers all 5 colored spans + " | " separators.
        char const_buf[128];
        int const_len = 0;
        bool any_const = false;
        for (int c = 0; c < 5; c++) {
            if (!const_seen[c]) continue;
            const_len += snprintf(const_buf + const_len, sizeof(const_buf) - (size_t)const_len,
                                  "%s%s", any_const ? " | " : "", const_names[c]);
            any_const = true;
        }
        if (!any_const) snprintf(const_buf, sizeof(const_buf), "none in view");

        ui_telemetry_set_signal(t, sig_snr, sig_has_snr, sig_const, sig_used,
                                n, used_count, const_buf);
    }

    lvgl_port_unlock();
}

// Zeroes the trip accumulator above -- wired to Home's Reset Trip button
// via ui_home_set_reset_trip_cb() below; the yes/no confirmation itself is
// entirely the UI's concern (main/ui_home.c), this just does the reset.
// Clears the have_prev_* flags too, not just the running totals -- otherwise
// the very next tick would compute one huge bogus distance/elevation delta
// against the stale pre-reset position/altitude instead of starting clean.
void gps_ui_bridge_reset_trip(void)
{
    s_trip_miles = 0.0f;
    s_max_mph = 0.0f;
    s_moving_ticks = 0;
    s_elev_gain_ft = 0.0f;
    s_have_prev_pos = false;
    s_have_prev_alt = false;
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
    ui_home_t *h = ui_home();
    if (h) ui_home_set_reset_trip_cb(h, gps_ui_bridge_reset_trip);

    xTaskCreate(bridge_task, "gps_ui_bridge", 4096, NULL, 3, NULL);
}
