/*
 * ui_telemetry.h — 2A Telemetry screen (instrument panel)
 */
#ifndef UI_TELEMETRY_H
#define UI_TELEMETRY_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pre-allocated bar objects -- one per satellite currently in view, up to
// this many; the rest stay hidden. Not gps.h's GPS_MAX_SATS (48): that's
// generous headroom for the data model, this is a display cap chosen for
// what's actually legible as individual bars across the card's width.
#define UI_TELEM_BARS 32

typedef struct {
    lv_obj_t *screen;
    ui_status_t status;

    // SPEED/TRUE HEADING/ALTITUDE MSL/SATELLITES/ACCURACY/TRIP were dropped
    // from this screen -- they just restated Home's own cards in a plainer
    // form. What's left is only what Home doesn't already show -- see
    // ui_telemetry_create()'s own comment.
    lv_obj_t *pos_dd;

    lv_obj_t *vspeed;
    lv_obj_t *hdop;

    // PDOP/VDOP/fix type: parsed from GSA (see gps.h's gps_state_t), same
    // "in view, unused elsewhere on this screen" reasoning as vspeed/hdop
    // above -- Home's ACCURACY card already covers the HDOP-derived
    // feet/meters number these three don't restate.
    lv_obj_t *pdop;
    lv_obj_t *vdop;
    lv_obj_t *fix_type;   /* "3D FIX" / "2D FIX" / "NO FIX" */

    lv_obj_t *local_caption;  /* "LOCAL | CDT" / "LOCAL | CST" -- see ui_telemetry_set_time() */
    lv_obj_t *local_time;
    lv_obj_t *utc_time;

    lv_obj_t *signal_caption;
    lv_obj_t *constellations;
    lv_obj_t *view_hint;      /* "TAP FOR SKY VIEW" / "TAP FOR BAR VIEW" -- see sky_toggle_cb() */
    lv_obj_t *bars_wrap;      /* flex-row container holding bars[] -- shown when !sky_mode */
    lv_obj_t *bars[UI_TELEM_BARS];
    lv_obj_t *sky_wrap;       /* polar sky-plot container -- shown when sky_mode */
    lv_obj_t *sky_dots[UI_TELEM_BARS];
    bool sky_mode;            /* false = bar chart, true = polar sky view; see sky_toggle_cb() */
} ui_telemetry_t;

ui_telemetry_t *ui_telemetry_create(lv_event_cb_t tab_cb);

/* ---- setters ------------------------------------------------------------ */
// valid == false (no fix yet this session) shows "--" instead of a
// formatted lat/lon -- same "no data" gap as this header's other setters,
// see ui_telemetry_set_vspeed()'s own comment.
void ui_telemetry_set_position(ui_telemetry_t *t, double dd_lat, double dd_lon, bool valid);
// value is already converted; unit is "fpm" or "m/min" per
// app_settings_get_elevation_m(). valid == false (no altitude fix yet)
// shows "--" instead of `value` -- see ui_home_set_speed()'s comment
// (main/ui_home.h) for why this matters: gps_ui_bridge.c's tick() only
// ever called these when the underlying GPS field was valid, so without a
// way to say "no data" they'd keep showing whatever number was last set
// (this screen's own creation-time demo value, on a device that never got
// a fix) instead of anything honest.
void ui_telemetry_set_vspeed(ui_telemetry_t *t, int value, const char *unit, bool valid);
void ui_telemetry_set_hdop(ui_telemetry_t *t, float hdop, bool valid);
// pdop/vdop are raw DOP values (lower = better, same convention as HDOP);
// fix_type is GSA field 3 as-is: 0 = not seen yet, 1 = no fix, 2 = 2D, 3 = 3D.
// valid == false blanks pdop/vdop to "--" too (fix_type's own "0 = not
// seen yet" case already reads fine as-is, kept for when GSA has reported
// *something* but this tick's dop_valid gate didn't pass).
void ui_telemetry_set_dop(ui_telemetry_t *t, float pdop, float vdop, int fix_type, bool valid);
// tz_abbrev updates the LOCAL card's own caption ("LOCAL | CDT"/"LOCAL |
// CST") -- see gps_ui_bridge.c's us_central_from_utc(), which flips it
// twice a year along with the actual UTC offset it applies.
void ui_telemetry_set_time(ui_telemetry_t *t, const char *local, const char *utc,
                           const char *tz_abbrev);
// One entry per satellite currently in view, n <= UI_TELEM_BARS (extras are
// silently dropped, not an error -- see UI_TELEM_BARS's own comment).
// constellation[i] is gps_constellation_t's GPS_CONST_* value (0=GPS,
// 1=GLONASS, 2=Galileo, 3=BeiDou, 4=QZSS) -- passed as a plain uint8_t
// rather than pulling in gps.h's enum, same reasoning as this screen's
// other setters staying decoupled from main/gps.h's types. snr[i] is
// 0-99 dB-Hz, meaningless (bar shows unlit) where has_snr[i] is false.
// used_count is the total number of satellites across all constellations
// actually contributing to the fix (used[] itself only says yes/no per
// bar) -- feeds the "N IN SOLUTION" caption. constellations is shown as-is
// in a recolor-enabled label (see ui_telemetry_create()) -- pass it with
// "#RRGGBB text#" spans (ui_theme.h's UI_C_*_HEX) if per-constellation
// color is wanted, or plain text otherwise.
void ui_telemetry_set_signal(ui_telemetry_t *t, const uint8_t *snr,
                             const bool *has_snr, const uint8_t *constellation,
                             const bool *used, int n, int used_count,
                             const char *constellations);

// Same per-satellite indexing/cap as ui_telemetry_set_signal() above (call
// both from the same loop) -- feeds the polar sky view's dot positions
// instead of the bar chart's heights. elevation_deg is 0-90 (0=horizon,
// 90=zenith); azimuth_deg is 0-359, clockwise from true north. Dots stay
// positioned/colored even while the sky view is hidden (see sky_toggle_cb())
// so it's already current the moment a tap reveals it.
void ui_telemetry_set_sky(ui_telemetry_t *t, const uint8_t *elevation_deg,
                          const uint16_t *azimuth_deg, const uint8_t *constellation,
                          const bool *used, int n);

#ifdef __cplusplus
}
#endif
#endif /* UI_TELEMETRY_H */
