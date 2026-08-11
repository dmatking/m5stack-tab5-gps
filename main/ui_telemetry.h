/*
 * ui_telemetry.h — 2A Telemetry screen (instrument panel)
 */
#ifndef UI_TELEMETRY_H
#define UI_TELEMETRY_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_TELEM_BARS 12

typedef struct {
    lv_obj_t *screen;
    ui_status_t status;

    lv_obj_t *speed;
    lv_obj_t *heading;

    lv_obj_t *pos_line1;
    lv_obj_t *pos_line2;
    lv_obj_t *pos_dd;

    lv_obj_t *altitude;
    lv_obj_t *vspeed;

    lv_obj_t *sats;
    lv_obj_t *hdop;
    lv_obj_t *accuracy;

    lv_obj_t *local_caption;  /* "LOCAL | CDT" / "LOCAL | CST" -- see ui_telemetry_set_time() */
    lv_obj_t *local_time;
    lv_obj_t *utc_time;

    lv_obj_t *trip;
    lv_obj_t *max_speed;
    lv_obj_t *moving;

    lv_obj_t *signal_caption;
    lv_obj_t *constellations;
    lv_obj_t *bars[UI_TELEM_BARS];
} ui_telemetry_t;

ui_telemetry_t *ui_telemetry_create(lv_event_cb_t tab_cb);

/* ---- setters ------------------------------------------------------------ */
void ui_telemetry_set_speed(ui_telemetry_t *t, float mph);
void ui_telemetry_set_heading(ui_telemetry_t *t, int deg);
void ui_telemetry_set_position(ui_telemetry_t *t, const char *ddm_lat,
                               const char *ddm_lon, double dd_lat, double dd_lon);
void ui_telemetry_set_altitude(ui_telemetry_t *t, int feet, int fpm);
void ui_telemetry_set_quality(ui_telemetry_t *t, int used, int visible,
                              float hdop, float accuracy_ft);
// tz_abbrev updates the LOCAL card's own caption ("LOCAL | CDT"/"LOCAL |
// CST") -- see gps_ui_bridge.c's us_central_from_utc(), which flips it
// twice a year along with the actual UTC offset it applies.
void ui_telemetry_set_time(ui_telemetry_t *t, const char *local, const char *utc,
                           const char *tz_abbrev);
void ui_telemetry_set_trip(ui_telemetry_t *t, float miles, float max_mph,
                           const char *moving);
/* snr[]: 0-99 dB-Hz per satellite; n <= UI_TELEM_BARS. */
void ui_telemetry_set_signal(ui_telemetry_t *t, const uint8_t *snr, int n,
                             int used, const char *constellations);

#ifdef __cplusplus
}
#endif
#endif /* UI_TELEMETRY_H */
