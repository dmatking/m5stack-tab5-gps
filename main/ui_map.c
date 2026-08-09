#include "ui_map.h"
#include <stdio.h>

static ui_map_t s_map;

/* Map viewport: full width minus side padding, full height minus header,
 * navbar and bottom padding. Kept as constants so lat/lon -> px conversion in
 * firmware can use the same numbers. */
#define MAP_W (UI_SCREEN_W - 2 * UI_PAD_SIDE)          /* 680 */
#define MAP_H (UI_SCREEN_H - 66 - UI_NAVBAR_H - 16)    /* 1082 */

static lv_obj_t *overlay_card(lv_obj_t *parent)
{
    lv_obj_t *c = ui_box(parent);
    lv_obj_set_style_bg_color(c, UI_C_CARD, 0);
    lv_obj_set_style_bg_opa(c, 240, 0);        /* ~94% like the mockup */
    lv_obj_set_style_border_color(c, lv_color_hex(0x223040), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, 16, 0);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 16, 0);
    return c;
}

static lv_obj_t *map_line(lv_obj_t *parent, lv_color_t color, lv_coord_t w,
                          bool rounded, bool dashed)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, rounded, 0);
    if (dashed) {
        lv_obj_set_style_line_dash_width(l, 26, 0);
        lv_obj_set_style_line_dash_gap(l, 14, 0);
    }
    lv_obj_set_pos(l, 0, 0);
    return l;
}

static lv_obj_t *zoom_key(lv_obj_t *parent, const char *text, bool accent)
{
    lv_obj_t *k = ui_box(parent);
    lv_obj_set_size(k, 88, 88);
    lv_obj_set_style_radius(k, 18, 0);
    lv_obj_set_style_bg_color(k, UI_C_CARD, 0);
    lv_obj_set_style_bg_opa(k, 240, 0);
    lv_obj_set_style_border_color(k, accent ? UI_C_BLUE : lv_color_hex(0x223040), 0);
    lv_obj_set_style_border_width(k, 1, 0);
    lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = ui_label(k, text, accent ? ui_font.semi_s : ui_font.semi_l,
                           accent ? UI_C_BLUE : UI_C_TEXT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    return k;
}

ui_map_t *ui_map_create(lv_event_cb_t tab_cb)
{
    ui_map_t *m = &s_map;
    lv_memzero(m, sizeof(*m));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    m->screen = scr;

    ui_status_create(scr, &m->status, true);

    /* map viewport ------------------------------------------------------- */
    lv_obj_t *holder = ui_box(scr);
    lv_obj_set_width(holder, LV_PCT(100));
    lv_obj_set_flex_grow(holder, 1);
    lv_obj_set_style_pad_hor(holder, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(holder, 16, 0);

    lv_obj_t *map = ui_box(holder);
    lv_obj_set_size(map, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(map, UI_C_MAP_BG, 0);
    lv_obj_set_style_bg_opa(map, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(map, UI_C_BORDER, 0);
    lv_obj_set_style_border_width(map, 1, 0);
    lv_obj_set_style_radius(map, UI_RADIUS, 0);
    lv_obj_set_style_clip_corner(map, true, 0);
    m->map = map;

#if UI_MAP_USE_CANVAS
    /* Tile target. Allocate the buffer in PSRAM in your firmware and pass it
     * in; LV_CANVAS_BUF_SIZE is a compile-time helper in LVGL 9. */
    static lv_color_t *canvas_buf; /* assign before ui_map_create() */
    m->canvas = lv_canvas_create(map);
    if (canvas_buf) {
        lv_canvas_set_buffer(m->canvas, canvas_buf, MAP_W, MAP_H,
                             LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(m->canvas, UI_C_MAP_BG, LV_OPA_COVER);
    }
    lv_obj_set_pos(m->canvas, 0, 0);
#else
    /* Schematic grid, drawn once. */
    for (int x = 80; x < MAP_W; x += 80) {
        lv_obj_t *g = ui_box(map);
        lv_obj_set_size(g, 1, MAP_H);
        lv_obj_set_pos(g, x, 0);
        lv_obj_set_style_bg_color(g, UI_C_MAP_GRID, 0);
        lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    }
    for (int y = 80; y < MAP_H; y += 80) {
        lv_obj_t *g = ui_box(map);
        lv_obj_set_size(g, MAP_W, 1);
        lv_obj_set_pos(g, 0, y);
        lv_obj_set_style_bg_color(g, UI_C_MAP_GRID, 0);
        lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    }

    /* Static road + trail geometry (replace with real data when you have it). */
    static const lv_point_precise_t road_pts[] = {
        {0, 700}, {190, 660}, {360, 600}, {540, 505}, {680, 455}
    };
    lv_obj_t *road = map_line(map, UI_C_MAP_ROAD, 16, true, false);
    lv_line_set_points(road, road_pts, 5);

    static const lv_point_precise_t trail_pts[] = {
        {300, MAP_H}, {340, 930}, {300, 760}, {360, 600}
    };
    lv_obj_t *trail = map_line(map, UI_C_MAP_TRACK, 9, true, false);
    lv_line_set_points(trail, trail_pts, 4);
#endif

    /* breadcrumb + route ------------------------------------------------- */
    m->track = map_line(map, UI_C_GREEN, 4, true, true);
    lv_obj_set_style_line_dash_width(m->track, 7, 0);
    lv_obj_set_style_line_dash_gap(m->track, 13, 0);
    lv_obj_set_style_line_opa(m->track, 130, 0);

    m->route = map_line(map, UI_C_BLUE, 7, true, true);

    /* own position: halo + dot ------------------------------------------- */
    m->me_halo = ui_box(map);
    lv_obj_set_size(m->me_halo, 104, 104);
    lv_obj_set_style_radius(m->me_halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m->me_halo, UI_C_GREEN, 0);
    lv_obj_set_style_bg_opa(m->me_halo, 30, 0);

    m->me = ui_box(map);
    lv_obj_set_size(m->me, 26, 26);
    lv_obj_set_style_radius(m->me, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m->me, UI_C_GREEN, 0);
    lv_obj_set_style_bg_opa(m->me, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(m->me, UI_C_BG, 0);
    lv_obj_set_style_border_width(m->me, 3, 0);

    /* destination marker (ring) ------------------------------------------ */
    m->marker = ui_box(map);
    lv_obj_set_size(m->marker, 34, 34);
    lv_obj_set_style_radius(m->marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(m->marker, UI_C_BLUE, 0);
    lv_obj_set_style_border_width(m->marker, 6, 0);

    /* floating cards ----------------------------------------------------- */
    m->dest_card = overlay_card(map);
    lv_obj_set_size(m->dest_card, MAP_W - 32, LV_SIZE_CONTENT);
    lv_obj_align(m->dest_card, LV_ALIGN_TOP_MID, 0, 16);
    ui_flex_row(m->dest_card, 12);
    lv_obj_set_flex_align(m->dest_card, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *dleft = ui_box(m->dest_card);
    lv_obj_set_size(dleft, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(dleft, 2);
    ui_caption(dleft, "NAVIGATING TO");
    m->dest_name = ui_label(dleft, "Gemini Bridges", ui_font.semi_m, UI_C_TEXT);

    lv_obj_t *dright = ui_box(m->dest_card);
    lv_obj_set_size(dright, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(dright, 2);
    lv_obj_set_flex_align(dright, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    m->dest_dist = ui_label(dright, "6.4 mi", ui_font.semi_l, UI_C_BLUE);
    m->dest_sub  = ui_label(dright, "brg 094\xC2\xB0 \xC2\xB7 ETA 14:50",
                            ui_font.xs, UI_C_MUTED);

    lv_obj_t *keys = ui_box(map);
    lv_obj_set_size(keys, 88, LV_SIZE_CONTENT);
    lv_obj_align(keys, LV_ALIGN_TOP_RIGHT, -16, 180);
    ui_flex_col(keys, 12);
    zoom_key(keys, "+", false);
    zoom_key(keys, "-", false);
    zoom_key(keys, "TRK\nUP", true);

    lv_obj_t *info = overlay_card(map);
    lv_obj_set_size(info, MAP_W - 32, LV_SIZE_CONTENT);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -16);
    ui_flex_row(info, 12);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *ileft = ui_box(info);
    lv_obj_set_size(ileft, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(ileft, 8);
    m->coords = ui_label(ileft, "32\xC2\xB0 54.1234' N \xC2\xB7 097\xC2\xB0 19.5678' W",
                         ui_font.semi_s, UI_C_TEXT);
    lv_obj_t *scale_row = ui_box(ileft);
    lv_obj_set_size(scale_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(scale_row, 12);
    lv_obj_set_flex_align(scale_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_t *ruler = ui_box(scale_row);          /* scale bar */
    lv_obj_set_size(ruler, 130, 10);
    lv_obj_set_style_border_color(ruler, UI_C_MUTED, 0);
    lv_obj_set_style_border_width(ruler, 2, 0);
    lv_obj_set_style_border_side(ruler,
        LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM, 0);
    m->scale_label = ui_label(scale_row, "0.5 mi", ui_font.xs, UI_C_MUTED);

    lv_obj_t *iright = ui_box(info);
    lv_obj_set_size(iright, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(iright, 4);
    lv_obj_set_flex_align(iright, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    m->zoom_label = ui_label(iright, "zoom 14", ui_font.xs, UI_C_MUTED);
    m->fix_label  = ui_label(iright, "3D fix \xC2\xB7 \xC2\xB1 9.4 ft", ui_font.xs, UI_C_GREEN);

    ui_navbar_create(scr, UI_TAB_MAP, tab_cb);

    /* default demo geometry */
    static const lv_point_precise_t demo_route[] = {{350, 660}, {420, 520}, {455, 350}};
    static const lv_point_precise_t demo_track[] = {{320, 1040}, {340, 920}, {310, 800}, {350, 660}};
    ui_map_set_route(m, demo_route, 3);
    ui_map_set_track(m, demo_track, 4);
    ui_map_set_own_marker(m, 350, 660);
    ui_map_set_dest_marker(m, 455, 350);
    return m;
}

/* ------------------------------------------------------------------ setters */

void ui_map_set_position(ui_map_t *m, const char *lat, const char *lon)
{
    if (!m) return;
    lv_label_set_text_fmt(m->coords, "%s \xC2\xB7 %s", lat, lon);
}

void ui_map_set_own_marker(ui_map_t *m, lv_coord_t x, lv_coord_t y)
{
    if (!m) return;
    lv_obj_set_pos(m->me, x - 13, y - 13);
    lv_obj_set_pos(m->me_halo, x - 52, y - 52);
}

void ui_map_set_destination(ui_map_t *m, const char *name, float dist_mi,
                            int bearing_deg, const char *eta)
{
    if (!m) return;
    if (name) lv_label_set_text(m->dest_name, name);
    lv_label_set_text_fmt(m->dest_dist, "%.1f mi", dist_mi);
    // Numeric spec formatted separately from the %s -- see the comment on
    // the equivalent fix in ui_nav.c's ui_nav_set_cross_track() for why
    // (a confirmed real crash mixing a float spec with %s in the same
    // lv_label_set_text_fmt() call; this one hadn't been exercised yet
    // but is the same risk shape, not worth waiting to find out).
    char brg_buf[8];
    snprintf(brg_buf, sizeof(brg_buf), "%03d", bearing_deg);
    lv_label_set_text_fmt(m->dest_sub, "brg %s\xC2\xB0 \xC2\xB7 ETA %s",
                          brg_buf, eta ? eta : "--:--");
}

void ui_map_set_dest_marker(ui_map_t *m, lv_coord_t x, lv_coord_t y)
{
    if (m) lv_obj_set_pos(m->marker, x - 17, y - 17);
}

void ui_map_set_route(ui_map_t *m, const lv_point_precise_t *pts, uint16_t n)
{
    if (!m || n > UI_MAP_MAX_ROUTE_PTS) return;
    lv_memcpy(m->route_pts, pts, n * sizeof(lv_point_precise_t));
    lv_line_set_points(m->route, m->route_pts, n);
}

void ui_map_set_track(ui_map_t *m, const lv_point_precise_t *pts, uint16_t n)
{
    if (!m || n > UI_MAP_MAX_ROUTE_PTS) return;
    lv_memcpy(m->track_pts, pts, n * sizeof(lv_point_precise_t));
    lv_line_set_points(m->track, m->track_pts, n);
}

void ui_map_set_zoom(ui_map_t *m, int zoom, const char *scale_text)
{
    if (!m) return;
    lv_label_set_text_fmt(m->zoom_label, "zoom %d", zoom);
    if (scale_text) lv_label_set_text(m->scale_label, scale_text);
}

void ui_map_set_fix(ui_map_t *m, const char *text, bool good)
{
    if (!m) return;
    lv_label_set_text(m->fix_label, text);
    lv_obj_set_style_text_color(m->fix_label, good ? UI_C_GREEN : UI_C_RED, 0);
}

void ui_map_show_navigation(ui_map_t *m, bool navigating)
{
    if (!m) return;
    lv_obj_t *hide[] = { m->dest_card, m->route, m->marker };
    for (int i = 0; i < 3; i++) {
        if (navigating) lv_obj_remove_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
        else            lv_obj_add_flag(hide[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_map_get_viewport(ui_map_t *m, lv_coord_t *w, lv_coord_t *h)
{
    LV_UNUSED(m);
    if (w) *w = MAP_W;
    if (h) *h = MAP_H;
}
