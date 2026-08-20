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
#include <string.h>

#include "app_settings.h"
#include "battery.h"
#include "design_ui.h"
#include "gps.h"
#include "sd_card.h"
#include "trip_store.h"
#include "waypoints.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GPS_UI_BRIDGE";

#define TICK_PERIOD_MS 500

// Formats one coordinate value per fmt (matches app_settings_get_coord_format()'s
// numbering, == ui_goto.h's ui_coord_fmt_t): 0=DD MM.MMMM (the original,
// only format this app had until Settings' Coordinate format row went from
// decorative to real), 1=DD.DDDDDD, 2=DD MM SS. Latitude degrees are 2
// digits (max 90), longitude 3 (max 180), matching the mockup's own
// original DDM formatting -- kept for the other two formats too, for the
// same reason: "097" not "97" reads as unambiguously 3-digit-wide at a
// glance, "32" doesn't need to.
static void format_coord(double decimal_deg, bool is_lat, int fmt, char *out, size_t out_size)
{
    double a = fabs(decimal_deg);
    int deg = (int)a;
    char hemi = is_lat ? (decimal_deg >= 0 ? 'N' : 'S')
                        : (decimal_deg >= 0 ? 'E' : 'W');
    if (fmt == 1) { // DD.DDDDDD
        snprintf(out, out_size, "%.6f\xC2\xB0 %c", a, hemi);
    } else if (fmt == 2) { // DD MM SS
        double min_full = (a - deg) * 60.0;
        int min = (int)min_full;
        double sec = (min_full - min) * 60.0;
        if (is_lat) snprintf(out, out_size, "%d\xC2\xB0 %02d' %04.1f\" %c", deg, min, sec, hemi);
        else        snprintf(out, out_size, "%03d\xC2\xB0 %02d' %04.1f\" %c", deg, min, sec, hemi);
    } else { // fmt == 0, DD MM.MMMM
        double min = (a - deg) * 60.0;
        if (is_lat) snprintf(out, out_size, "%d\xC2\xB0 %07.4f' %c", deg, min, hemi);
        else        snprintf(out, out_size, "%03d\xC2\xB0 %07.4f' %c", deg, min, hemi);
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

// ---- Distance/speed and Elevation unit conversion ----------------------
// Every computation in this file works in mph/mi/ft internally (matching
// gps.c's own knots/meters -> mph/ft conversions that predate this feature)
// -- these convert to km/h/km/m only at the point a value is handed to a
// screen setter, per app_settings_get_distance_km()/_get_elevation_m().
// Kept as tiny wrappers rather than inlining the constants at each call
// site, so the mi->km/ft->m factors exist in exactly one place each.
static float conv_speed_mph(float mph, bool km)    { return km ? mph * 1.609344f : mph; }
static float conv_dist_mi(float mi, bool km)        { return km ? mi * 1.609344f : mi; }
static float conv_elev_ft(float ft, bool m)         { return m  ? ft * 0.3048f   : ft; }
static const char *speed_unit_str(bool km) { return km ? "km/h" : "mph"; }
static const char *dist_unit_str(bool km)  { return km ? "km"   : "mi"; }
static const char *elev_unit_str(bool m)   { return m  ? "m"    : "ft"; }

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
// Telemetry trip accumulator below and, since Goto/Nav went from decorative
// to real, the Nav screen's distance-to-destination too.
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

// Initial great-circle bearing from point 1 to point 2, degrees true,
// 0-360. Standard forward-azimuth formula -- only the Nav screen needs this
// (Telemetry/Home's trip odometer only ever needed distance).
static float bearing_deg(double lat1, double lon1, double lat2, double lon2)
{
    double phi1 = lat1 * (GPS_UI_PI / 180.0);
    double phi2 = lat2 * (GPS_UI_PI / 180.0);
    double dlambda = (lon2 - lon1) * (GPS_UI_PI / 180.0);
    double y = sin(dlambda) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dlambda);
    double theta = atan2(y, x) * (180.0 / GPS_UI_PI);
    return (float)fmod(theta + 360.0, 360.0);
}

// Cross-track error, statute miles: how far the current position (3) is
// off the direct great-circle course from the leg's origin (1) to its
// destination (2). Positive = right of course, negative = left -- matches
// ui_nav_set_cross_track()'s offset convention directly, no sign flip
// needed at the call site. Standard cross-track-distance formula, reusing
// haversine_miles()/bearing_deg() above rather than a separate trig path:
//   XTE = asin(sin(d13/R) * sin(theta13 - theta12)) * R
static float cross_track_miles(double olat, double olon, double dlat, double dlon,
                                double plat, double plon)
{
    const double r_mi = 3958.8;
    double d13     = (double)haversine_miles(olat, olon, plat, plon);
    double theta13 = (double)bearing_deg(olat, olon, plat, plon) * (GPS_UI_PI / 180.0);
    double theta12 = (double)bearing_deg(olat, olon, dlat, dlon) * (GPS_UI_PI / 180.0);
    return (float)(asin(sin(d13 / r_mi) * sin(theta13 - theta12)) * r_mi);
}

// Telemetry's (and now Home's, see ui_home_set_trip() below) trip/max-speed/
// moving-time/elevation-gain accumulator. Persisted now (main/trip_store.h)
// rather than purely session-only -- gps_ui_bridge_reset_trip() (wired to
// Home's Reset Trip button, see ui_home_set_reset_trip_cb()) still zeroes
// it on demand, but a plain power cycle no longer does the same thing by
// accident (a real trip lost to a hard power-off outdoors, not a reset
// button, is what motivated this). Distance/moving-time/elevation-gain
// only accrue above MOVING_MPH_THRESHOLD, to keep GPS position/altitude
// jitter while parked from slowly inflating the trip odometer.
#define MOVING_MPH_THRESHOLD 1.15f  // ~1 knot

static bool   s_have_prev_pos;
static double s_prev_lat, s_prev_lon;
static float  s_trip_miles;
static float  s_max_mph;
static uint32_t s_moving_ticks;
static float  s_elev_gain_ft;

// Persistence for the four totals above -- deliberately NOT saved every
// tick (2Hz over a multi-hour drive is 7000+ NVS writes/hour for no
// benefit, real flash wear for a total nobody's reading that often).
// s_trip_save_tick throttles to roughly once every TRIP_SAVE_TICKS ticks;
// s_trip_last_saved is compared against the current totals so a save is
// skipped entirely while parked/no-fix and nothing's actually changed.
#define TRIP_SAVE_TICKS (30000 / TICK_PERIOD_MS)  // ~30s
static int          s_trip_save_tick;
static trip_totals_t s_trip_last_saved;

// Vertical speed (fpm) from consecutive altitude readings, 500ms apart --
// noisy on its own (GPS altitude jitter amplified ~120x by the short
// interval), so smoothed with a simple exponential moving average rather
// than shown raw.
static bool  s_have_prev_alt;
static float s_prev_alt_ft;
static float s_vspeed_fpm_ema;

// Battery percent (main/battery.c, INA226 on the internal I2C bus) --
// re-read every ~10s (TICK_PERIOD_MS * 20), same throttling and reasoning
// as this file's own SD-usage read below: a number that changes over
// minutes/hours doesn't need a fresh I2C transaction every 500ms tick.
// s_battery_have stays false (every screen keeps whatever ui_status_create()
// seeded it with) if the chip never responded at boot -- see
// battery_read()'s own comment on why that's not papered over with
// a fake number. s_battery_external tracks its other real state (no pack
// installed, running on USB power alone) -- see battery.h's own comment.
static int  s_battery_tick;
static bool s_battery_have;
static int  s_battery_pct;
static bool s_battery_external;

// Cross-track's leg origin -- the position navigation *started* from,
// which is what defines the course line XTE is measured against. Armed on
// the ui_is_navigating() false->true edge and latched on the first tick
// with a valid fix after that, since there may not be one at the exact
// moment "Start Navigation" is tapped. Cleared implicitly by the next arm
// (s_leg_have goes false again), not on Stop -- Stop just leaves the Nav
// screen showing whatever it last showed, same as every other card here.
static bool   s_was_navigating;
static bool   s_leg_armed;
static bool   s_leg_have;
static double s_leg_lat, s_leg_lon;

// Smooths "closing"/"opening" (is |XTE| trending back toward the course
// line) against 2Hz GPS jitter -- compares this tick's |XTE| to last
// tick's with a small dead-band, holding the previous verdict inside the
// dead-band rather than flipping on noise. Reset alongside the leg origin
// so a brand new leg doesn't compare against the previous one's stale
// magnitude.
static bool  s_xte_have_prev;
static float s_xte_prev_abs;
static bool  s_xte_closing;

// Every field in the status bar is a global value -- fix state, sat count,
// local time, battery -- none of it is screen-specific, so every screen's
// bar gets fed identically through here rather than each call site doing
// its own thing. That divergence is exactly what left Nav/Goto/Map showing
// ui_status_create()'s "GPS FIX"/"14 sats"/"10:24 AM" demo strings forever
// (only Home and Telemetry were ever wired for fix/sats/clock; battery got
// added to the rest later). Harmless while those bars rendered nothing at
// all, actively misleading once they did -- a stale green "GPS FIX" next to
// a live battery percent reads as a real fix. clock_buf NULL = no valid
// local time yet, leave whatever's there alone.
static void push_status(ui_status_t *s, bool fix, int sats, const char *clock_buf)
{
    if (!s) return;
    ui_status_set_fix(s, fix ? "GPS FIX" : "NO FIX", fix);
    ui_status_set_sats(s, sats);
    if (clock_buf) ui_status_set_clock(s, clock_buf);
    if (s_battery_have) {
        if (s_battery_external) ui_status_set_battery_external(s);
        else                    ui_status_set_battery(s, s_battery_pct);
    }
}

static void tick(void)
{
    gps_state_t st = gps_get_state();
    bool fix = gps_has_fix();

    // Which constellations currently have >=1 satellite in view, and how
    // many satellites total are used in the fix -- computed once here
    // (unlocked, cheap) rather than per-screen, since both Telemetry's
    // SIGNAL legend and Settings' Constellations row want it.
    bool const_seen[5] = { false, false, false, false, false };
    int sats_used_total = 0;
    for (int i = 0; i < st.satellite_count; i++) {
        const gps_satellite_t *sat = &st.satellites[i];
        if (sat->used_in_solution) sats_used_total++;
        if (sat->constellation < 5) const_seen[sat->constellation] = true;
    }

    if (++s_battery_tick >= 20) {
        s_battery_tick = 0;
        s_battery_have = battery_read(&s_battery_pct, &s_battery_external);
    }

    // Cross-track leg-origin arming -- see s_leg_armed's own comment. Runs
    // every tick regardless of fix state, so the false->true edge is never
    // missed even if navigation starts with no fix yet.
    bool navigating_now = ui_is_navigating();
    if (navigating_now && !s_was_navigating) {
        s_leg_armed = true;
        s_leg_have  = false;
    }
    s_was_navigating = navigating_now;

    if (!lvgl_port_lock(50)) return;

    ui_home_t *h = ui_home();
    if (!h) {
        lvgl_port_unlock();
        return;
    }

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
    char status_clock[24];
    const char *status_clock_p = NULL;   // NULL until there's a real time to show
    if (have_local) {
        us_central_from_utc(&st.utc_tm, &local_tm, &tz_abbrev);
        char time_part[16];
        format_clock(time_part, sizeof(time_part), local_tm.tm_hour, local_tm.tm_min, 0, false);
        snprintf(status_clock, sizeof(status_clock), "%s %s", time_part, tz_abbrev);
        status_clock_p = status_clock;
    }

    // Every status bar, one call each -- see push_status()'s own comment.
    // Settings is deliberately absent: its header is hand-rolled rather than
    // ui_status_create()'d (no fix dot or sat count to fill in) and it wants
    // the clock without the timezone suffix, so it's fed separately below.
    push_status(&h->status, fix, st.sats_in_use, status_clock_p);
    ui_telemetry_t *tel_ui = ui_telemetry();
    if (tel_ui) push_status(&tel_ui->status, fix, st.sats_in_use, status_clock_p);
    ui_nav_t *nav_status_ui = ui_nav();
    if (nav_status_ui) push_status(&nav_status_ui->status, fix, st.sats_in_use, status_clock_p);
    ui_goto_t *goto_ui = ui_goto();
    if (goto_ui) push_status(&goto_ui->status, fix, st.sats_in_use, status_clock_p);
    // Map's LVGL screen is never actually shown (ui_shell_enter_map() stops
    // LVGL and hands the panel to the native renderer, which draws its own
    // status bar) -- fed anyway for consistency, costs nothing.
    ui_map_t *map_ui = ui_map();
    if (map_ui) push_status(&map_ui->status, fix, st.sats_in_use, status_clock_p);

    // Position/speed/altitude/accuracy: always called now, every tick,
    // `valid` passed straight through from the corresponding gps_state_t
    // field -- see ui_home_set_speed()'s own comment (main/ui_home.h) for
    // why. Used to be called only `if (st.X_valid)`, which meant a device
    // that never got a fix (confirmed on real hardware: NO FIX/0 sats
    // sitting right next to a confident "42 mph"/"1,248 ft") just kept
    // showing ui_home_create()'s literal creation-time demo numbers
    // forever, since nothing ever told these cards there was no real data
    // yet. Position already took plain strings (no signature change
    // needed there) -- "--" is passed directly instead of a formatted
    // coordinate when invalid.
    int coord_fmt = app_settings_get_coord_format();
    if (st.latlon_valid) {
        char lat_buf[32], lon_buf[32];
        format_coord(st.latitude_deg, true, coord_fmt, lat_buf, sizeof(lat_buf));
        format_coord(st.longitude_deg, false, coord_fmt, lon_buf, sizeof(lon_buf));
        ui_home_set_position(h, lat_buf, lon_buf);
    } else {
        ui_home_set_position(h, "--", "--");
    }

    bool dist_km = app_settings_get_distance_km();
    bool elev_m  = app_settings_get_elevation_m();

    {
        float mph = st.speed_valid ? st.speed_knots * 1.15078f : 0.0f;
        ui_home_set_speed(h, conv_speed_mph(mph, dist_km), speed_unit_str(dist_km), st.speed_valid);
    }

    // Gated on heading_valid (gps.h) rather than shown unconditionally --
    // GPS course-over-ground is noise at rest, so the compass now reads
    // "---" standing still instead of a confident stale bearing.
    ui_home_set_heading(h, (int)(st.heading_deg + 0.5f),
                        cardinal_8(st.heading_deg), st.heading_valid);

    {
        float alt_ft = st.altitude_valid ? st.altitude_m * 3.28084f : 0.0f;
        ui_home_set_altitude(h, (int)(conv_elev_ft(alt_ft, elev_m) + 0.5f),
                             elev_unit_str(elev_m), st.altitude_valid);
    }

    {
        float acc_ft = st.hdop_valid ? hdop_to_accuracy_ft(st.hdop) : 0.0f;
        ui_home_set_accuracy(h, conv_elev_ft(acc_ft, elev_m), elev_unit_str(elev_m), st.hdop_valid);
    }

    const char *sat_quality = st.sats_in_use >= 7 ? "Good"
                             : st.sats_in_use >= 4 ? "Fair"
                                                   : "Poor";
    ui_home_set_satellites(h, st.sats_in_use, sat_quality);

    // Same "always call it" fix as position/speed/altitude/accuracy above --
    // ui_home_set_local_time() already takes plain strings (no signature
    // change needed), so the fix here is just not skipping the call: a
    // device with no valid time/date yet was stuck showing
    // ui_home_create()'s literal creation-time demo clock/date forever.
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
    } else {
        ui_home_set_local_time(h, "--:--", "", "--", "--");
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
    ui_telemetry_t *t = tel_ui;   // status bar already fed above
    if (t) {
        float speed_mph = 0.0f;
        bool moving_now = false;
        if (st.speed_valid) {
            speed_mph = st.speed_knots * 1.15078f;
            moving_now = speed_mph > MOVING_MPH_THRESHOLD;
            if (speed_mph > s_max_mph) s_max_mph = speed_mph;
            if (moving_now) s_moving_ticks++;
        }

        if (st.latlon_valid) {
            ui_telemetry_set_position(t, st.latitude_deg, st.longitude_deg, true);

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
        } else {
            ui_telemetry_set_position(t, 0.0, 0.0, false);
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
            // Same axis as elevation (a rate of elevation change), so it
            // follows app_settings_get_elevation_m() too -- fpm becomes
            // m/min rather than being a separate unit choice of its own.
            float vspeed_disp = elev_m ? vspeed_fpm * 0.3048f : vspeed_fpm;
            ui_telemetry_set_vspeed(t, (int)(vspeed_disp + 0.5f), elev_m ? "m/min" : "fpm", true);
        } else {
            ui_telemetry_set_vspeed(t, 0, elev_m ? "m/min" : "fpm", false);
        }

        ui_telemetry_set_hdop(t, st.hdop, st.hdop_valid);
        ui_telemetry_set_dop(t, st.pdop, st.vdop, st.fix_type, st.dop_valid);

        // Unlike Home's single (now Central) clock, Telemetry has room for
        // both a real LOCAL (Central) and a real UTC card side by side --
        // no UTC-vs-local tradeoff to make here, just show both for real.
        if (have_local) {
            char local_buf[16], utc_buf[16];
            format_clock(local_buf, sizeof(local_buf), local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec, true);
            format_clock(utc_buf, sizeof(utc_buf), st.utc_tm.tm_hour, st.utc_tm.tm_min, st.utc_tm.tm_sec, true);
            ui_telemetry_set_time(t, local_buf, utc_buf, tz_abbrev);
        } else {
            ui_telemetry_set_time(t, "--:--:--", "--:--:--", "--");
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
        ui_home_set_trip(h, conv_dist_mi(s_trip_miles, dist_km), moving_hms_buf,
                         conv_speed_mph(avg_mph, dist_km), conv_speed_mph(s_max_mph, dist_km),
                         (int)(conv_elev_ft(s_elev_gain_ft, elev_m) + 0.5f),
                         dist_unit_str(dist_km), speed_unit_str(dist_km), elev_unit_str(elev_m));

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
        uint8_t  sig_snr[UI_TELEM_BARS];
        bool     sig_has_snr[UI_TELEM_BARS];
        uint8_t  sig_const[UI_TELEM_BARS];
        bool     sig_used[UI_TELEM_BARS];
        uint8_t  sig_elev[UI_TELEM_BARS];
        uint16_t sig_azim[UI_TELEM_BARS];
        int n = st.satellite_count;
        for (int i = 0; i < n && i < UI_TELEM_BARS; i++) {
            const gps_satellite_t *sat = &st.satellites[i];
            sig_snr[i]      = sat->snr;
            sig_has_snr[i]  = sat->has_snr;
            sig_const[i]    = (uint8_t)sat->constellation;
            sig_used[i]     = sat->used_in_solution;
            sig_elev[i]     = sat->elevation_deg;
            sig_azim[i]     = sat->azimuth_deg;
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
                                n, sats_used_total, const_buf);
        // Polar sky view -- same satellites, same tick, just a different
        // pair of fields (elevation/azimuth instead of SNR) feeding a
        // different (currently hidden, see sky_toggle_cb()) visualization.
        ui_telemetry_set_sky(t, sig_elev, sig_azim, sig_const, sig_used, n);
    }

    // ---- Nav screen (active navigation) ------------------------------------
    // Goto/Nav went from decorative to real (see design_ui.c's goto_start_cb(),
    // which wires ui_goto_parse() into ui_set_destination()) -- distance/
    // bearing/closure/ETA/cross-track computed fresh every tick from the
    // live position, whatever destination was set when "Start Navigation"
    // was tapped, and (for cross-track) the leg origin latched above.
    ui_nav_t *nav_ui = nav_status_ui;   // status bar already fed above
    double dest_lat, dest_lon;
    // Gated on `fix`, not just latlon_valid -- the latter is sticky in
    // gps.c (set on the first good sentence, never cleared), so on its own
    // it would keep this whole block computing confident distances and
    // bearings from a stale position long after signal was lost, directly
    // under a status bar reading NO FIX.
    bool nav_active = nav_ui && ui_is_navigating() &&
                      ui_get_destination(&dest_lat, &dest_lon);
    if (nav_active && (!fix || !st.latlon_valid)) {
        ui_nav_set_stale(nav_ui);
    } else if (nav_active) {
        // Latch the cross-track leg origin on the first valid fix after
        // arming (see s_leg_armed's own comment) -- exactly here, not
        // earlier, because this is the first point in the tick where a
        // fix AND a destination are both known to be good.
        if (s_leg_armed) {
            s_leg_lat = st.latitude_deg;
            s_leg_lon = st.longitude_deg;
            s_leg_have = true;
            s_leg_armed = false;
            s_xte_have_prev = false;
            s_xte_closing = true;
        }

        float dist_mi = haversine_miles(st.latitude_deg, st.longitude_deg, dest_lat, dest_lon);
        float brg = bearing_deg(st.latitude_deg, st.longitude_deg, dest_lat, dest_lon);
        int heading = (int)(st.heading_deg + 0.5f);
        float nav_speed_mph = st.speed_valid ? st.speed_knots * 1.15078f : 0.0f;

        ui_nav_set_bearing(nav_ui, (int)(brg + 0.5f), heading, st.heading_valid);
        ui_nav_set_distance(nav_ui, conv_dist_mi(dist_mi, dist_km), dist_unit_str(dist_km));
        ui_nav_set_speed(nav_ui, conv_speed_mph(nav_speed_mph, dist_km), speed_unit_str(dist_km));

        // Velocity made good -- current speed projected onto the bearing
        // to the destination, so it reads near-zero/negative when moving
        // across or away from it instead of always showing full raw speed
        // regardless of direction actually traveled. Only computable with a
        // real heading to project onto (gps.h's heading_valid): without one
        // the cosine term is meaningless, and it feeds ETA/time-to-go too,
        // so all three blank together rather than propagating the garbage.
        bool vmg_valid = st.heading_valid && st.speed_valid;
        float vmg_mph = 0.0f;
        if (vmg_valid) {
            float angle_diff = (float)((brg - heading) * (GPS_UI_PI / 180.0));
            vmg_mph = nav_speed_mph * cosf(angle_diff);
            ui_nav_set_closure(nav_ui, conv_speed_mph(vmg_mph, dist_km), speed_unit_str(dist_km));
        } else {
            ui_nav_set_closure_unknown(nav_ui);
        }

        if (vmg_valid && vmg_mph > 0.5f) {
            float hours_to_go = dist_mi / vmg_mph;
            char time_to_go[16];
            char eta_text[16] = "--:--";
            // Past a day, BOTH of these stop meaning anything, so they
            // degrade together rather than one of them quietly lying:
            // time-to-go overflows a card laid out for "17:56" once the
            // hour count goes 3 digits, and the ETA below is a same-day
            // minutes-of-day wrap, so a 30-hour leg would render as a
            // perfectly plausible time *today*.
            //
            // Written !(x < 24) rather than (x >= 24) deliberately: a NaN
            // from a degenerate leg fails every comparison, so this form
            // sends it down the ">24h" branch instead of letting it reach
            // the cast below (float->uint32_t of a NaN is undefined).
            if (!(hours_to_go < 24.0f)) {
                lv_strlcpy(time_to_go, "> 24h", sizeof(time_to_go));
            } else {
                uint32_t secs_to_go = (uint32_t)(hours_to_go * 3600.0f);
                snprintf(time_to_go, sizeof(time_to_go), "%u:%02u",
                         (unsigned)(secs_to_go / 3600), (unsigned)((secs_to_go % 3600) / 60));
                if (have_local) {
                    // Simple minutes-of-day add-and-wrap, same spirit as
                    // this file's other clock math -- fine for a same-day
                    // ETA, not a real calendar-aware add (which is exactly
                    // why the >=24h case above skips it). Formatted through
                    // format_clock() so it honours the 12/24-hour setting
                    // like every other clock in the app; it used to be a
                    // bare "%d:%02d", the one holdout.
                    int total_min = local_tm.tm_hour * 60 + local_tm.tm_min +
                                    (int)(hours_to_go * 60.0f);
                    total_min = ((total_min % 1440) + 1440) % 1440;
                    format_clock(eta_text, sizeof(eta_text),
                                 total_min / 60, total_min % 60, 0, false);
                }
            }
            ui_nav_set_eta(nav_ui, eta_text, time_to_go);
        } else {
            ui_nav_set_eta(nav_ui, "--:--", "--:--");
        }

        // Cross track -- needs the leg origin latched above in addition to
        // the live fix and destination already required by nav_active.
        // The degenerate origin==destination check guards a leg that
        // latched while already standing on the destination: bearing_deg()
        // returns a defined-but-meaningless 0 deg for that (atan2(0,0)),
        // and there's no course line to be off of in the first place.
        bool xte_ready = s_leg_have &&
                         (s_leg_lat != dest_lat || s_leg_lon != dest_lon);
        if (xte_ready) {
            float xte_mi = cross_track_miles(s_leg_lat, s_leg_lon, dest_lat, dest_lon,
                                             st.latitude_deg, st.longitude_deg);
            float xte_abs = xte_mi < 0.0f ? -xte_mi : xte_mi;

            const float deadband_mi = 0.005f;
            if (s_xte_have_prev) {
                float delta = xte_abs - s_xte_prev_abs;
                if (delta > deadband_mi)       s_xte_closing = false;
                else if (delta < -deadband_mi) s_xte_closing = true;
                // else: inside the dead-band, hold the previous verdict.
            }
            s_xte_prev_abs = xte_abs;
            s_xte_have_prev = true;

            float full_scale_mi = 0.5f;
            ui_nav_set_cross_track(nav_ui, conv_dist_mi(xte_mi, dist_km),
                                   dist_unit_str(dist_km),
                                   conv_dist_mi(full_scale_mi, dist_km),
                                   s_xte_closing, true);
        } else {
            ui_nav_set_cross_track(nav_ui, 0.0f, dist_unit_str(dist_km), 0.5f, true, false);
        }
    }

    // ---- Settings screen ---------------------------------------------------
    // DISPLAY (brightness/keep-screen-on), LOGGING & STORAGE, and UNITS &
    // FORMAT (coordinate format, distance/speed, elevation, 24-hour time)
    // are all fully real now. Constellations/Update rate/Time zone below
    // are real *displays* of measured/computed values, not editable
    // settings -- this app has no write path to the GPS module to actually
    // reconfigure its constellations or update rate (TX line wired but
    // unused, see gps.c's file header), and Time zone is Central-only by
    // design (see us_central_from_utc()'s own comment), not a picker.
    ui_settings_t *set_ui = ui_settings();
    if (set_ui) {
        if (s_battery_have) {
            if (s_battery_external) ui_status_set_battery_external(&set_ui->status);
            else                    ui_status_set_battery(&set_ui->status, s_battery_pct);
        }
        ui_settings_set_track_log(set_ui, gps_log_active());

        static const char *const_abbrev[5] = { "GPS", "GLO", "GAL", "BEI", "QZS" };
        char const_short[48];
        int const_short_len = 0;
        bool any_const_short = false;
        for (int c = 0; c < 5; c++) {
            if (!const_seen[c]) continue;
            const_short_len += snprintf(const_short + const_short_len,
                                        sizeof(const_short) - (size_t)const_short_len,
                                        "%s%s", any_const_short ? " + " : "", const_abbrev[c]);
            any_const_short = true;
        }
        // Same "always call it" fix as Home/Telemetry above -- these three
        // used to only ever update `if` their backing data was available,
        // so a device that never got any of it just kept showing
        // ui_settings_create()'s own creation-time demo values ("GPS + GLO
        // + GAL", "5 Hz", "CDT (UTC-5)") forever, same root cause as the
        // Home dashboard freeze this whole fix addresses.
        ui_settings_set_value(set_ui, UI_SET_CONSTELLATIONS,
                              any_const_short ? const_short : "--");

        if (st.fix_rate_valid) {
            char rate_buf[16];
            snprintf(rate_buf, sizeof(rate_buf), "%.0f Hz", st.fix_rate_hz);
            ui_settings_set_value(set_ui, UI_SET_UPDATE_RATE, rate_buf);
        } else {
            ui_settings_set_value(set_ui, UI_SET_UPDATE_RATE, "--");
        }

        if (have_local) {
            // Central-only (see this block's own comment above) -- CDT/CST
            // are the only two abbreviations us_central_from_utc() ever
            // returns, so their UTC offsets can just be hardcoded here
            // rather than computed generically.
            char tz_buf[24];
            snprintf(tz_buf, sizeof(tz_buf), "%s (UTC%s)", tz_abbrev,
                     strcmp(tz_abbrev, "CDT") == 0 ? "-5" : "-6");
            ui_settings_set_value(set_ui, UI_SET_TIMEZONE, tz_buf);

            // This screen's header is hand-built (ui_settings_create()),
            // not ui_status_create() like every other screen's, and got
            // missed when the rest of the app went from decorative to
            // real -- stuck at its creation-time "10:24 AM" forever
            // (spotted directly by the user). No tz suffix, matching that
            // original text -- just the time, same as the header had room
            // for before.
            char time_part[16];
            format_clock(time_part, sizeof(time_part), local_tm.tm_hour, local_tm.tm_min, 0, false);
            ui_status_set_clock(&set_ui->status, time_part);
        } else {
            // Same fix, same reasoning as the header comment above -- this
            // branch just didn't exist before, so a device with no valid
            // time yet kept the original "10:24 AM"/timezone-less state.
            ui_settings_set_value(set_ui, UI_SET_TIMEZONE, "--");
            ui_status_set_clock(&set_ui->status, "--:--");
        }

        // SD usage walks the FAT free-cluster chain (esp_vfs_fat_info()) --
        // real work, unlike everything else this task touches. Once every
        // ~10s (TICK_PERIOD_MS * 20) is plenty for a number nobody's
        // watching change in real time.
        static int s_storage_tick;
        if (++s_storage_tick >= 20) {
            s_storage_tick = 0;
            float used_gb, total_gb;
            if (sd_card_get_usage(&used_gb, &total_gb)) {
                ui_settings_set_storage(set_ui, used_gb, total_gb);
            }
        }

        // Line 1 (FW version/serial number) has nothing real to show yet --
        // kept as the same static placeholder ui_settings_create() shows at
        // boot, just re-passed every tick since ui_settings_set_footer()
        // always sets both lines together. Only line 2's uptime is real.
        // "AT6668", not the original demo text's "u-blox M10" -- see
        // gps.c's own file header for the real chipset (M5Stack GPS Module
        // v2.1). The demo text named the wrong vendor entirely.
        int64_t uptime_s = esp_timer_get_time() / 1000000;
        char line2[48];
        snprintf(line2, sizeof(line2), "AT6668 | uptime %d:%02d:%02d",
                 (int)(uptime_s / 3600), (int)((uptime_s % 3600) / 60), (int)(uptime_s % 60));
        ui_settings_set_footer(set_ui, "Tab5 | FW 1.4.2 | SN 0A31-7742", line2);
    }

    // Map/Goto have nothing else to update here -- Map runs its own native
    // drag/zoom loop and Goto its own local input handling. Their status
    // bars are fed with everyone else's up at the top of this function.

    lvgl_port_unlock();

    // Trip persistence -- deliberately outside the lvgl lock above: a real
    // flash write (nvs_commit()) can take tens of ms, and there's no reason
    // to hold LVGL's redraw hostage to that when these are plain statics
    // this same task already owns exclusively. See TRIP_SAVE_TICKS' own
    // comment for why this isn't done every tick.
    if (++s_trip_save_tick >= TRIP_SAVE_TICKS) {
        s_trip_save_tick = 0;
        trip_totals_t now = {
            .distance_mi  = s_trip_miles,
            .max_mph      = s_max_mph,
            .moving_s     = (uint32_t)(s_moving_ticks * (TICK_PERIOD_MS / 1000.0f)),
            .elev_gain_ft = s_elev_gain_ft,
        };
        if (memcmp(&now, &s_trip_last_saved, sizeof(now)) != 0) {
            trip_store_save(&now);
            s_trip_last_saved = now;
        }
    }
}

// Zeroes the trip accumulator above -- wired to Home's Reset Trip button
// via ui_home_set_reset_trip_cb() below; the yes/no confirmation itself is
// entirely the UI's concern (main/ui_home.c), this just does the reset.
// Clears the have_prev_* flags too, not just the running totals -- otherwise
// the very next tick would compute one huge bogus distance/elevation delta
// against the stale pre-reset position/altitude instead of starting clean.
// Also clears the persisted copy immediately, same as the in-RAM one --
// waiting up to TRIP_SAVE_TICKS for the next periodic save would mean a
// reset that doesn't survive a power cycle for the next half-minute.
void gps_ui_bridge_reset_trip(void)
{
    s_trip_miles = 0.0f;
    s_max_mph = 0.0f;
    s_moving_ticks = 0;
    s_elev_gain_ft = 0.0f;
    s_have_prev_pos = false;
    s_have_prev_alt = false;
    trip_store_clear();
    s_trip_last_saved = (trip_totals_t){ 0 };
    s_trip_save_tick = 0;
}

void gps_ui_bridge_mark_waypoint(void)
{
    // Runs on the LVGL task (LVGL's lock is held by the click dispatch) and
    // takes gps.c's mutex inside gps_get_state(). tick() acquires the same
    // two in the opposite order but never holds both -- gps_get_state()
    // copies and releases well before lvgl_port_lock(). Don't "tidy"
    // gps_get_state() into tick()'s locked section: that would close the
    // gap into a real lock-order inversion.
    gps_state_t st = gps_get_state();

    const char *msg;
    if (!gps_has_fix() || !st.latlon_valid) {
        msg = "No GPS fix";
    } else {
        waypoint_t w;
        switch (waypoints_add(st.latitude_deg, st.longitude_deg, &w)) {
        case WAYPOINTS_OK:        msg = w.name;        break;
        case WAYPOINTS_ERR_FULL:  msg = "Storage full"; break;
        default:                  msg = "Save failed";  break;
        }
    }
    ui_home_flash_mark(ui_home(), msg);
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
    if (h) {
        ui_home_set_reset_trip_cb(h, gps_ui_bridge_reset_trip);
        ui_home_set_mark_cb(h, gps_ui_bridge_mark_waypoint);
    }

    // Resume wherever the last session's trip left off, rather than
    // starting every boot at zero -- see trip_store.h's own comment for
    // why this exists. s_have_prev_pos/s_have_prev_alt stay false (their
    // normal boot-time default): there's no previous *tick* to diff
    // against yet, only a previously-saved running total to carry
    // forward, so the first fresh position/altitude this session still
    // starts its own delta cleanly instead of comparing against wherever
    // the device was when it was last saved.
    trip_totals_t saved;
    trip_store_load(&saved);
    s_trip_miles    = saved.distance_mi;
    s_max_mph       = saved.max_mph;
    s_moving_ticks  = (uint32_t)((uint64_t)saved.moving_s * 1000 / TICK_PERIOD_MS);
    s_elev_gain_ft  = saved.elev_gain_ft;
    s_trip_last_saved = saved;

    xTaskCreate(bridge_task, "gps_ui_bridge", 4096, NULL, 3, NULL);
}
