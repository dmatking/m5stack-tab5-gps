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

    lv_obj_t *local_caption;  /* "LOCAL | CDT" / "LOCAL | CST" -- see ui_telemetry_set_time() */
    lv_obj_t *local_time;
    lv_obj_t *utc_time;

    lv_obj_t *signal_caption;
    lv_obj_t *constellations;
    lv_obj_t *bars[UI_TELEM_BARS];
} ui_telemetry_t;

ui_telemetry_t *ui_telemetry_create(lv_event_cb_t tab_cb);

/* ---- setters ------------------------------------------------------------ */
void ui_telemetry_set_position(ui_telemetry_t *t, double dd_lat, double dd_lon);
void ui_telemetry_set_vspeed(ui_telemetry_t *t, int fpm);
void ui_telemetry_set_hdop(ui_telemetry_t *t, float hdop);
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

#ifdef __cplusplus
}
#endif
#endif /* UI_TELEMETRY_H */
