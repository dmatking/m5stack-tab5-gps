/*
 * ui_map.h — 2A Map screen: map layer + floating destination / position cards
 *
 * The map layer is a plain container you draw into. Two options:
 *   1. default — lv_line objects for grid, roads, breadcrumb track and route
 *      (what this file builds; no extra memory, no image decoder)
 *   2. UI_MAP_USE_CANVAS 1 — an lv_canvas you can blit real tiles into;
 *      720x1000 at 16bpp needs ~1.4 MB, so put the buffer in PSUM/PSRAM
 */
#ifndef UI_MAP_H
#define UI_MAP_H

#include "ui_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef UI_MAP_USE_CANVAS
#define UI_MAP_USE_CANVAS 0
#endif

#define UI_MAP_MAX_ROUTE_PTS 32

typedef struct {
    lv_obj_t *screen;
    ui_status_t status;

    lv_obj_t *map;            /* clipped map viewport, parent for layers  */
#if UI_MAP_USE_CANVAS
    lv_obj_t *canvas;         /* tile target when enabled                 */
#endif
    lv_obj_t *route;          /* lv_line: bearing/route line to target    */
    lv_obj_t *track;          /* lv_line: breadcrumb history              */
    lv_obj_t *marker;         /* destination marker                       */
    lv_obj_t *me;             /* own-position dot + halo                  */
    lv_obj_t *me_halo;

    lv_obj_t *dest_name;
    lv_obj_t *dest_dist;
    lv_obj_t *dest_sub;       /* "brg 094° · ETA 14:50"                   */
    lv_obj_t *dest_card;

    lv_obj_t *coords;
    lv_obj_t *scale_label;
    lv_obj_t *zoom_label;
    lv_obj_t *fix_label;

    lv_point_precise_t route_pts[UI_MAP_MAX_ROUTE_PTS];
    lv_point_precise_t track_pts[UI_MAP_MAX_ROUTE_PTS];
} ui_map_t;

ui_map_t *ui_map_create(lv_event_cb_t tab_cb);

/* ---- setters ------------------------------------------------------------ */
void ui_map_set_position(ui_map_t *m, const char *lat, const char *lon);
void ui_map_set_own_marker(ui_map_t *m, lv_coord_t x, lv_coord_t y);
void ui_map_set_destination(ui_map_t *m, const char *name, float dist_mi,
                            int bearing_deg, const char *eta);
void ui_map_set_dest_marker(ui_map_t *m, lv_coord_t x, lv_coord_t y);
void ui_map_set_route(ui_map_t *m, const lv_point_precise_t *pts, uint16_t n);
void ui_map_set_track(ui_map_t *m, const lv_point_precise_t *pts, uint16_t n);
void ui_map_set_zoom(ui_map_t *m, int zoom, const char *scale_text);
void ui_map_set_fix(ui_map_t *m, const char *text, bool good);
void ui_map_show_navigation(ui_map_t *m, bool navigating);

/* Viewport size in px, for converting lat/lon to screen coordinates. */
void ui_map_get_viewport(ui_map_t *m, lv_coord_t *w, lv_coord_t *h);

#ifdef __cplusplus
}
#endif
#endif /* UI_MAP_H */
