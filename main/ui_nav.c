#include "ui_nav.h"
#include <math.h>
#include <stdio.h>

static ui_nav_t s_nav;

#define ARROW_SIZE 220
#define ARROW_R    82.0f

/* Small metric card used in the 2x2 grid. */
static lv_obj_t *stat_card(lv_obj_t *parent, const char *caption,
                           const char *value, lv_color_t color,
                           lv_obj_t **out_value)
{
    lv_obj_t *c = ui_card(parent);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    ui_flex_col(c, 2);
    ui_caption(c, caption);
    *out_value = ui_label(c, value, ui_font.num_m, color);
    return c;
}

static lv_obj_t *arrow_line(lv_obj_t *parent, lv_coord_t w)
{
    lv_obj_t *l = lv_line_create(parent);
    lv_obj_set_style_line_color(l, UI_C_BLUE, 0);
    lv_obj_set_style_line_width(l, w, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, 0, 0);
    return l;
}

ui_nav_t *ui_nav_create(lv_event_cb_t tab_cb)
{
    ui_nav_t *n = &s_nav;
    lv_memzero(n, sizeof(*n));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    n->screen = scr;

    ui_status_create(scr, &n->status, true);

    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    ui_flex_col(body, 12);

    /* destination header --------------------------------------------------- */
    lv_obj_t *dest = ui_card(body);
    lv_obj_set_width(dest, LV_PCT(100));
    lv_obj_set_height(dest, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(dest, 20, 0);
    lv_obj_set_style_pad_ver(dest, 14, 0);
    ui_flex_row(dest, 12);
    lv_obj_set_flex_align(dest, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *dleft = ui_box(dest);
    lv_obj_set_size(dleft, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(dleft, 2);
    ui_caption(dleft, "DESTINATION");
    n->dest_name = ui_label(dleft, "Gemini Bridges", ui_font.semi_m, UI_C_TEXT);

    n->dest_meta = ui_label(dest, "WPT 07\n32\xC2\xB0 59.6' N", ui_font.xs, UI_C_MUTED);
    lv_obj_set_style_text_align(n->dest_meta, LV_TEXT_ALIGN_RIGHT, 0);

    /* bearing rose --------------------------------------------------------- */
    lv_obj_t *rose = ui_card(body);
    lv_obj_set_width(rose, LV_PCT(100));
    lv_obj_set_height(rose, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(rose, 14, 0);
    ui_flex_col(rose, 8);
    lv_obj_set_flex_align(rose, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(rose, "RELATIVE BEARING");

    lv_obj_t *area = ui_box(rose);
    lv_obj_set_size(area, ARROW_SIZE, ARROW_SIZE);
    n->arrow_area = area;

    lv_obj_t *scale = lv_scale_create(area);
    lv_obj_set_size(scale, ARROW_SIZE, ARROW_SIZE);
    lv_obj_center(scale);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_label_show(scale, false);
    lv_scale_set_total_tick_count(scale, 73);
    lv_scale_set_major_tick_every(scale, 6);
    lv_scale_set_range(scale, 0, 360);
    lv_scale_set_angle_range(scale, 360);
    lv_scale_set_rotation(scale, 270);
    lv_obj_set_style_arc_opa(scale, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_line_color(scale, UI_C_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 7, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, UI_C_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 12, LV_PART_INDICATOR);

    lv_obj_t *inner = ui_box(area);
    lv_obj_set_size(inner, ARROW_SIZE - 34, ARROW_SIZE - 34);
    lv_obj_center(inner);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(inner, lv_color_hex(0x2A3542), 0);
    lv_obj_set_style_border_width(inner, 1, 0);

    lv_obj_t *north = ui_box(area);
    lv_obj_set_size(north, 9, 9);
    lv_obj_align(north, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_set_style_radius(north, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(north, UI_C_GREEN, 0);
    lv_obj_set_style_bg_opa(north, LV_OPA_COVER, 0);

    n->arrow_shaft  = arrow_line(area, 16);
    n->arrow_barb_l = arrow_line(area, 14);
    n->arrow_barb_r = arrow_line(area, 14);

    lv_obj_t *rrow = ui_box(rose);
    lv_obj_set_size(rrow, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(rrow, 0);
    lv_obj_set_flex_align(rrow, LV_FLEX_ALIGN_SPACE_AROUND,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    n->brg  = ui_label(rrow, "BRG 094\xC2\xB0", ui_font.s, UI_C_TEXT);
    n->hdg  = ui_label(rrow, "HDG 067\xC2\xB0", ui_font.s, UI_C_TEXT);
    n->turn = ui_label(rrow, "TURN R 27\xC2\xB0", ui_font.s, UI_C_BLUE);

    /* distance to go ------------------------------------------------------- */
    lv_obj_t *dist = ui_card(body);
    lv_obj_set_width(dist, LV_PCT(100));
    lv_obj_set_height(dist, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(dist, 22, 0);
    lv_obj_set_style_pad_ver(dist, 10, 0);
    ui_flex_row(dist, 12);
    lv_obj_set_flex_align(dist, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_caption(dist, "DISTANCE TO GO");
    lv_obj_t *dgrp = ui_box(dist);
    lv_obj_set_size(dgrp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(dgrp, 12);
    lv_obj_set_flex_align(dgrp, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    n->distance = ui_label(dgrp, "6.4", ui_font.num_xl, UI_C_TEXT);
    ui_label(dgrp, "mi", ui_font.semi_m, UI_C_GREEN);

    /* 2x2 stats ------------------------------------------------------------ */
    lv_obj_t *g1 = ui_box(body);
    lv_obj_set_size(g1, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(g1, 12);
    stat_card(g1, "CLOSURE (VMG)", "+21.4", UI_C_GREEN, &n->closure);
    stat_card(g1, "ETA", "14:50", UI_C_TEXT, &n->eta);

    lv_obj_t *g2 = ui_box(body);
    lv_obj_set_size(g2, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(g2, 12);
    stat_card(g2, "TIME TO GO", "17:56", UI_C_TEXT, &n->time_to_go);
    stat_card(g2, "SPEED", "24", UI_C_TEXT, &n->speed);

    /* cross track ---------------------------------------------------------- */
    lv_obj_t *xtk = ui_card(body);
    lv_obj_set_width(xtk, LV_PCT(100));
    lv_obj_set_height(xtk, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(xtk, 20, 0);
    lv_obj_set_style_pad_ver(xtk, 14, 0);
    ui_flex_col(xtk, 10);

    lv_obj_t *xhead = ui_box(xtk);
    lv_obj_set_size(xhead, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(xhead, 8);
    lv_obj_set_flex_align(xhead, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_caption(xhead, "CROSS TRACK");
    n->xtk_label = ui_label(xhead, "0.12 mi left \xC2\xB7 closing", ui_font.xs, UI_C_GREEN);

    lv_obj_t *rail = ui_box(xtk);
    lv_obj_set_size(rail, LV_PCT(100), 24);
    n->xtk_track = rail;

    lv_obj_t *line = ui_box(rail);
    lv_obj_set_size(line, LV_PCT(100), 3);
    lv_obj_align(line, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(line, 2, 0);
    lv_obj_set_style_bg_color(line, UI_C_BORDER, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    lv_obj_t *centre = ui_box(rail);
    lv_obj_set_size(centre, 3, LV_PCT(100));
    lv_obj_align(centre, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(centre, lv_color_hex(0x2A3542), 0);
    lv_obj_set_style_bg_opa(centre, LV_OPA_COVER, 0);

    n->xtk_dot = ui_box(rail);
    lv_obj_set_size(n->xtk_dot, 22, 22);
    lv_obj_set_style_radius(n->xtk_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(n->xtk_dot, UI_C_GREEN, 0);
    lv_obj_set_style_bg_opa(n->xtk_dot, LV_OPA_COVER, 0);
    lv_obj_align(n->xtk_dot, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *xscale = ui_box(xtk);
    lv_obj_set_size(xscale, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(xscale, 0);
    lv_obj_set_flex_align(xscale, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_label(xscale, "0.5 L", ui_font.xs, UI_C_DIM);
    ui_label(xscale, "on track", ui_font.xs, UI_C_DIM);
    ui_label(xscale, "0.5 R", ui_font.xs, UI_C_DIM);

    /* footer --------------------------------------------------------------- */
    lv_obj_t *spacer = ui_box(body);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t *btns = ui_box(body);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(btns, 12);
    lv_obj_t *bl = ui_box(btns);
    lv_obj_set_size(bl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bl, 1);
    n->btn_map = ui_button(bl, "Map View", UI_C_BLUE_BTN, UI_C_TEXT, false,
                           UI_C_BLUE, 92);
    lv_obj_t *br = ui_box(btns);
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(br, 1);
    n->btn_stop = ui_button(br, "Stop Nav", UI_C_CARD, UI_C_RED, true,
                            lv_color_hex(0x63303A), 92);

    ui_navbar_create(scr, UI_TAB_NAV, tab_cb);

    ui_nav_set_bearing(n, 94, 67);
    ui_nav_set_cross_track(n, -0.12f, true);
    return n;
}

/* ------------------------------------------------------------------ setters */

void ui_nav_set_destination(ui_nav_t *n, const char *name, const char *meta)
{
    if (!n) return;
    if (name) lv_label_set_text(n->dest_name, name);
    if (meta) lv_label_set_text(n->dest_meta, meta);
}

void ui_nav_set_bearing(ui_nav_t *n, int bearing_deg, int heading_deg)
{
    if (!n) return;

    int rel = ((bearing_deg - heading_deg) % 360 + 360) % 360;
    float a  = (float)rel * 3.14159265f / 180.0f;   /* 0 = straight ahead */
    float cx = ARROW_SIZE / 2.0f, cy = ARROW_SIZE / 2.0f;
    float dx = sinf(a), dy = -cosf(a);

    float tipx  = cx + dx * ARROW_R,        tipy  = cy + dy * ARROW_R;
    float tailx = cx - dx * ARROW_R * 0.62f, taily = cy - dy * ARROW_R * 0.62f;

    n->shaft_pts[0].x = (lv_value_precise_t)tailx;
    n->shaft_pts[0].y = (lv_value_precise_t)taily;
    n->shaft_pts[1].x = (lv_value_precise_t)tipx;
    n->shaft_pts[1].y = (lv_value_precise_t)tipy;
    lv_line_set_points(n->arrow_shaft, n->shaft_pts, 2);

    /* barbs: back from the tip at ±145° */
    const float barb = ARROW_R * 0.52f;
    for (int side = 0; side < 2; side++) {
        float b  = a + (side ? -1.0f : 1.0f) * 2.53f;   /* ~145 deg */
        float bx = tipx + sinf(b) * barb;
        float by = tipy - cosf(b) * barb;
        lv_point_precise_t *p = side ? n->barb_r_pts : n->barb_l_pts;
        p[0].x = (lv_value_precise_t)tipx;
        p[0].y = (lv_value_precise_t)tipy;
        p[1].x = (lv_value_precise_t)bx;
        p[1].y = (lv_value_precise_t)by;
        lv_line_set_points(side ? n->arrow_barb_r : n->arrow_barb_l, p, 2);
    }

    lv_label_set_text_fmt(n->brg, "BRG %03d\xC2\xB0", ((bearing_deg % 360) + 360) % 360);
    lv_label_set_text_fmt(n->hdg, "HDG %03d\xC2\xB0", ((heading_deg % 360) + 360) % 360);

    int turn = rel > 180 ? 360 - rel : rel;
    if (turn <= 2)      lv_label_set_text(n->turn, "ON COURSE");
    else if (rel > 180) lv_label_set_text_fmt(n->turn, "TURN L %d\xC2\xB0", turn);
    else                lv_label_set_text_fmt(n->turn, "TURN R %d\xC2\xB0", turn);
    lv_obj_set_style_text_color(n->turn, turn <= 2 ? UI_C_GREEN : UI_C_BLUE, 0);
}

void ui_nav_set_distance(ui_nav_t *n, float miles)
{
    if (!n) return;
    if (miles < 10.0f) lv_label_set_text_fmt(n->distance, "%.2f", miles);
    else               lv_label_set_text_fmt(n->distance, "%.1f", miles);
}

void ui_nav_set_closure(ui_nav_t *n, float vmg_mph)
{
    if (!n) return;
    lv_label_set_text_fmt(n->closure, "%+.1f", vmg_mph);
    lv_obj_set_style_text_color(n->closure,
                                vmg_mph > 0 ? UI_C_GREEN : UI_C_RED, 0);
}

void ui_nav_set_eta(ui_nav_t *n, const char *eta_text, const char *time_to_go)
{
    if (!n) return;
    if (eta_text)   lv_label_set_text(n->eta, eta_text);
    if (time_to_go) lv_label_set_text(n->time_to_go, time_to_go);
}

void ui_nav_set_speed(ui_nav_t *n, float mph)
{
    if (n) lv_label_set_text_fmt(n->speed, "%d", (int)(mph + 0.5f));
}

void ui_nav_set_cross_track(ui_nav_t *n, float offset_mi, bool closing)
{
    if (!n) return;

    float clamped = offset_mi;
    if (clamped >  0.5f) clamped =  0.5f;
    if (clamped < -0.5f) clamped = -0.5f;

    lv_coord_t w = lv_obj_get_width(n->xtk_track);
    if (w <= 0) w = UI_SCREEN_W - 2 * UI_PAD_SIDE - 40;
    lv_obj_align(n->xtk_dot, LV_ALIGN_CENTER,
                 (lv_coord_t)(clamped / 0.5f * (w / 2 - 11)), 0);

    float mag = offset_mi < 0 ? -offset_mi : offset_mi;
    // Format the float separately, then assemble with %s-only args --
    // confirmed on real hardware that mixing a %f/%.2f spec with %s specs
    // in the same lv_label_set_text_fmt() call corrupts the later %s
    // pointers (crashed inside LVGL's builtin lv_vsnprintf_inner ->
    // lv_strnlen on a garbage ~0xe0000000 address, addr2line-confirmed).
    // Root cause not fully nailed down -- possibly a float/pointer va_arg
    // extraction mismatch specific to this RISC-V toolchain's variadic ABI
    // -- but avoiding the mix entirely sidesteps it regardless of cause.
    char mag_buf[16];
    snprintf(mag_buf, sizeof(mag_buf), "%.2f", mag);
    lv_label_set_text_fmt(n->xtk_label, "%s mi %s \xC2\xB7 %s", mag_buf,
                          offset_mi < 0 ? "left" : "right",
                          closing ? "closing" : "opening");
    lv_obj_set_style_text_color(n->xtk_label, closing ? UI_C_GREEN : UI_C_RED, 0);
    lv_obj_set_style_bg_color(n->xtk_dot,
                              mag <= 0.25f ? UI_C_GREEN : UI_C_RED, 0);
}

void ui_nav_set_buttons(ui_nav_t *n, lv_event_cb_t map_cb, lv_event_cb_t stop_cb)
{
    if (!n) return;
    if (map_cb)  lv_obj_add_event_cb(n->btn_map,  map_cb,  LV_EVENT_CLICKED, NULL);
    if (stop_cb) lv_obj_add_event_cb(n->btn_stop, stop_cb, LV_EVENT_CLICKED, NULL);
}
