#include "ui_telemetry.h"

#include <math.h>

// Local pi, not libc's M_PI -- not guaranteed defined by this toolchain's
// math.h (same reasoning/value as gps_ui_bridge.c's own GPS_UI_PI).
static const float UI_TELEM_PI = 3.14159265358979323846f;

// Fixed square footprint (px) of the polar sky-plot drawn inside sky_wrap.
#define UI_TELEM_SKY_D 420

// Diameter of the outer (elevation=0) ring -- smaller than UI_TELEM_SKY_D,
// leaving a margin inside plot's own box for the N/E/S/W labels. LVGL clips
// children to their parent's box by default, independently at every level
// of the tree (plot clips its own children unless plot itself is flagged
// overflow-visible, then its parent independently does the same to plot's
// whole rendering, and so on) -- chasing that up through sky/sig/body once
// already turned into a losing game of whack-a-mole. Keeping the labels
// inside plot's actual bounds sidesteps the whole thing instead. See
// ui_telemetry_create()'s sky view and ui_telemetry_set_sky()'s dx/dy math,
// which both need this same ring radius (not UI_TELEM_SKY_D's).
#define UI_TELEM_RING_D 380

static ui_telemetry_t s_tel;

static void sky_toggle_cb(lv_event_t *e)
{
    ui_telemetry_t *t = lv_event_get_user_data(e);
    if (!t) return;
    t->sky_mode = !t->sky_mode;
    if (t->sky_mode) {
        lv_obj_add_flag(t->bars_wrap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(t->sky_wrap, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(t->view_hint, "TAP FOR BAR VIEW");
    } else {
        lv_obj_remove_flag(t->bars_wrap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(t->sky_wrap, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(t->view_hint, "TAP FOR SKY VIEW");
    }
}

static lv_obj_t *cell(lv_obj_t *parent, int grow, const char *caption,
                      const char *value, const lv_font_t *font,
                      lv_color_t color, lv_obj_t **out_value)
{
    lv_obj_t *c = ui_card(parent);
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(c, grow);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    ui_flex_col(c, 2);
    // Every card on this screen defaulted to flex's own start/start (top-
    // left) alignment -- unlike Home's equivalent cards, which all
    // explicitly center -- and read as a layout mistake once actually
    // looked at side by side rather than one card at a time.
    //
    // Main-axis CENTER (not START): matters only when a card ends up
    // taller than its own content -- currently just row3's FIX TYPE cell,
    // force-matched to PDOP/VDOP's height below since it uses a shorter
    // font -- where START would leave the caption+value pinned to the top
    // with a dead gap underneath instead of centered like its row-mates.
    // A no-op everywhere else: every other cell is already exactly as
    // tall as its content, so there's no leftover space to place within.
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(c, caption);
    *out_value = ui_label(c, value, font, color);
    return c;
}

static lv_obj_t *row(lv_obj_t *parent)
{
    lv_obj_t *r = ui_box(parent);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(r, 12);
    return r;
}

ui_telemetry_t *ui_telemetry_create(lv_event_cb_t tab_cb)
{
    ui_telemetry_t *t = &s_tel;
    lv_memzero(t, sizeof(*t));

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    t->screen = scr;

    ui_status_create(scr, &t->status, true);

    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    ui_flex_col(body, 12);

    // SPEED/TRUE HEADING/ALTITUDE MSL/SATELLITES/TRIP row all dropped from
    // this screen -- they just restated Home's own cards in a plainer form
    // (Home's HEADING is a compass dial vs. this screen's old bare "067°";
    // Home's TRIP has 5 fields + a Reset button vs. this screen's old 3
    // with none). What's left is only what Home *doesn't* show: vertical
    // speed, raw HDOP, UTC time, decimal-degree coordinates, and the
    // per-satellite SIGNAL chart.

    /* position ------------------------------------------------------------- */
    lv_obj_t *pos = ui_card(body);
    lv_obj_set_width(pos, LV_PCT(100));
    lv_obj_set_height(pos, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pos, 20, 0);
    lv_obj_set_style_pad_ver(pos, 16, 0);
    ui_flex_col(pos, 6);
    // "|" not "\xC2\xB7" (·) throughout this file -- see ui_home.c's ±
    // comment; same missing-glyph issue (LVGL's built-in Montserrat fonts
    // only include ASCII + the degree sign), different character, same
    // ASCII-substitute fix.
    lv_obj_set_flex_align(pos, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_caption(pos, "POSITION | WGS84");
    // Only the decimal-degree line -- the DD MM.MMMM lines this card used
    // to also show duplicated Home's CURRENT POSITION card exactly; DD is
    // the one coordinate format Home doesn't have.
    t->pos_dd = ui_label(pos, "32.902057, -97.326130 | DD", ui_font.semi_l, UI_C_TEXT);

    lv_obj_t *row1 = row(body);
    cell(row1, 1, "VERTICAL SPEED", "+210 fpm", ui_font.num_m, UI_C_TEXT, &t->vspeed);
    cell(row1, 1, "HDOP",           "0.8",      ui_font.num_m, UI_C_GREEN, &t->hdop);

    lv_obj_t *row2 = row(body);
    // Not using cell() here (unlike every other card on this screen) --
    // its caption is static, and this one needs to flip between "LOCAL |
    // CDT" and "LOCAL | CST" as daylight time comes and goes (see
    // ui_telemetry_set_time()), so the caption label itself needs to
    // survive past creation the same way t->local_time already does.
    lv_obj_t *local_card = ui_card(row2);
    lv_obj_set_height(local_card, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(local_card, 1);
    lv_obj_set_style_pad_hor(local_card, 20, 0);
    lv_obj_set_style_pad_ver(local_card, 14, 0);
    ui_flex_col(local_card, 2);
    lv_obj_set_flex_align(local_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    t->local_caption = ui_caption(local_card, "LOCAL | CDT");
    t->local_time = ui_label(local_card, "10:24:18", ui_font.num_m, UI_C_TEXT);
    cell(row2, 1, "UTC", "15:24:18", ui_font.num_m, UI_C_MUTED, &t->utc_time);

    /* fix quality: PDOP/VDOP (GSA, not surfaced anywhere else) + 2D/3D fix -- */
    lv_obj_t *row3 = row(body);
    lv_obj_t *pdop_cell = cell(row3, 1, "PDOP", "1.4", ui_font.num_m, UI_C_GREEN, &t->pdop);
    cell(row3, 1, "VDOP", "1.8", ui_font.num_m, UI_C_GREEN, &t->vdop);
    // Text, not digits -- semi_m (not num_m) same as ui_home.c's other
    // text-valued cells (e.g. its trip_cell() moving-time value).
    lv_obj_t *fix_cell = cell(row3, 1, "FIX TYPE", "3D FIX", ui_font.semi_m, UI_C_GREEN, &t->fix_type);
    // semi_m's line height is shorter than num_m's big digits, which left
    // this card visibly shorter than PDOP/VDOP beside it -- cell() sizes
    // every card to LV_SIZE_CONTENT and flex has no per-item cross-axis
    // stretch, so just measure the tallest row-mate once layout settles
    // and match it explicitly.
    lv_obj_update_layout(row3);
    lv_obj_set_height(fix_cell, lv_obj_get_height(pdop_cell));

    /* signal --------------------------------------------------------------- */
    lv_obj_t *sig = ui_card(body);
    lv_obj_set_width(sig, LV_PCT(100));
    lv_obj_set_flex_grow(sig, 1);
    lv_obj_set_style_pad_hor(sig, 20, 0);
    lv_obj_set_style_pad_ver(sig, 16, 0);
    ui_flex_col(sig, 10);
    lv_obj_set_flex_align(sig, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    // Whole card toggles bar chart <-> polar sky view on tap (see
    // sky_toggle_cb()) -- simplest hit target, and there's nothing else on
    // this card worth tapping separately. bars_wrap/sky_wrap below both use
    // flex_grow(1) so whichever is visible fills all the leftover height
    // this card's own flex_grow(1) already claims from body -- same
    // footprint either way, no layout jump when switching.
    //
    // Every ui_box() is clickable by default (lv_obj_constructor() sets
    // LV_OBJ_FLAG_CLICKABLE unconditionally, see lv_obj.c -- ui_box() only
    // strips SCROLLABLE) and LVGL hit-testing targets the deepest clickable
    // object under the point, not the nearest ancestor with a handler, so
    // every ui_box() child below (shead/bars_wrap/each bar/sky_wrap/plot/
    // each dot) would otherwise silently steal the tap instead of it
    // reaching sig. Each one gets LV_OBJ_FLAG_CLICKABLE removed right after
    // creation so sig is the only clickable object anywhere in this card.
    lv_obj_add_flag(sig, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sig, sky_toggle_cb, LV_EVENT_CLICKED, t);

    lv_obj_t *shead = ui_box(sig);
    lv_obj_remove_flag(shead, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(shead, LV_PCT(100), LV_SIZE_CONTENT);
    // Stacked, not a side-by-side row -- with real data (not the original
    // 3-constellation demo string) the constellation list can run to all 5
    // names ("GPS | GLONASS | GALILEO | BEIDOU | QZSS"), which overlapped
    // the caption in the same row instead of wrapping (confirmed on real
    // hardware). A column has nothing to overlap regardless of how long
    // either line gets.
    ui_flex_col(shead, 4);
    lv_obj_set_flex_align(shead, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    // "-" not "\xE2\x80\x94" (em dash) -- same missing-glyph issue as the ·
    // fix above, just a different character (also outside LVGL's built-in
    // font's ASCII+degree-sign-only coverage).
    t->signal_caption  = ui_caption(shead, "SIGNAL - 14 IN SOLUTION");
    t->constellations  = ui_label(shead, "GPS | GLONASS | GALILEO",
                                  ui_font.xs, UI_C_MUTED);
    lv_obj_set_width(t->constellations, LV_PCT(100));
    // The label itself still spans the full card width (needed for
    // wrapping) even though shead now centers it as a block -- centering
    // the *text within* that width needs its own text-align, same
    // reasoning as trip_cell()'s two-line captions on Home.
    lv_obj_set_style_text_align(t->constellations, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(t->constellations, LV_LABEL_LONG_WRAP);
    // Recolor mode: gps_ui_bridge.c sends this label's text with inline
    // "#RRGGBB text#" spans, one color per constellation matching that
    // constellation's bars below (see UI_C_*_HEX in ui_theme.h) -- lets a
    // single label show 5 different colors instead of one uniform tint.
    // The " | " separators between spans render in this label's own base
    // color (UI_C_MUTED, set above) since they're outside any tag.
    lv_label_set_recolor(t->constellations, true);

    t->view_hint = ui_label(shead, "TAP FOR SKY VIEW", ui_font.xs, UI_C_DIM);

    lv_obj_t *bars = ui_box(sig);
    lv_obj_remove_flag(bars, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(bars, LV_PCT(100));
    lv_obj_set_flex_grow(bars, 1);
    ui_flex_row(bars, 8);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    t->bars_wrap = bars;
    // All UI_TELEM_BARS created up front, hidden until ui_telemetry_set_signal()
    // has real satellites to show -- hidden flex items don't take up row
    // space, so however many end up visible auto-fill the width via
    // flex_grow, same as if only that many had ever been created. Color/
    // height/visibility all come from real data now; no demo values.
    for (int i = 0; i < UI_TELEM_BARS; i++) {
        t->bars[i] = ui_box(bars);
        lv_obj_remove_flag(t->bars[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_grow(t->bars[i], 1);
        lv_obj_set_style_radius(t->bars[i], 3, 0);
        lv_obj_set_style_bg_opa(t->bars[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(t->bars[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* sky: polar elevation/azimuth view, hidden until sky_toggle_cb() flips
     * to it -- not a flex container itself (plot below is absolute-centered
     * via lv_obj_center(), independent of whatever height flex_grow(1)
     * ends up giving this wrapper). */
    lv_obj_t *sky = ui_box(sig);
    lv_obj_remove_flag(sky, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(sky, LV_PCT(100));
    lv_obj_set_flex_grow(sky, 1);
    lv_obj_add_flag(sky, LV_OBJ_FLAG_HIDDEN);
    t->sky_wrap = sky;

    lv_obj_t *plot = ui_box(sky);
    lv_obj_remove_flag(plot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(plot, UI_TELEM_SKY_D, UI_TELEM_SKY_D);
    lv_obj_center(plot);

    // Elevation rings at 0/30/60 deg (90=zenith is the plot's own center,
    // no ring needed for it), sized off UI_TELEM_RING_D (not the bigger
    // UI_TELEM_SKY_D plot box -- see that macro's own comment for why) --
    // same hairline-circle style as ui_compass()'s inner ring in
    // ui_theme.c, just three of them (and a bit thicker/brighter --
    // ui_compass()'s 1px 0x2A3542 read as nearly invisible here on a much
    // bigger ring where it has more room to matter).
    for (int ring = 0; ring < 3; ring++) {
        float elev_ring = ring * 30.0f;
        lv_coord_t d = (lv_coord_t)(UI_TELEM_RING_D * (1.0f - elev_ring / 90.0f));
        lv_obj_t *circle = ui_box(plot);
        lv_obj_remove_flag(circle, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(circle, d, d);
        lv_obj_center(circle);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(circle, lv_color_hex(0x445870), 0);
        lv_obj_set_style_border_width(circle, 2, 0);
    }

    // Sit in the margin between the outer ring and plot's own edge (see
    // UI_TELEM_RING_D) -- inward offsets, so they stay inside plot's box
    // (no clipping to fight) while still landing clear of the ring line.
    lv_obj_t *lbl_n = ui_label(plot, "N", ui_font.xs, UI_C_MUTED);
    lv_obj_align(lbl_n, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_t *lbl_e = ui_label(plot, "E", ui_font.xs, UI_C_MUTED);
    lv_obj_align(lbl_e, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_t *lbl_s = ui_label(plot, "S", ui_font.xs, UI_C_MUTED);
    lv_obj_align(lbl_s, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_t *lbl_w = ui_label(plot, "W", ui_font.xs, UI_C_MUTED);
    lv_obj_align(lbl_w, LV_ALIGN_LEFT_MID, 6, 0);

    // All UI_TELEM_BARS dots created up front, same hidden-until-real-data
    // convention as t->bars[] above -- ui_telemetry_set_sky() positions/
    // colors/reveals however many satellites are actually in view.
    for (int i = 0; i < UI_TELEM_BARS; i++) {
        t->sky_dots[i] = ui_box(plot);
        lv_obj_remove_flag(t->sky_dots[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(t->sky_dots[i], 16, 16);
        lv_obj_set_style_radius(t->sky_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(t->sky_dots[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(t->sky_dots[i], LV_OBJ_FLAG_HIDDEN);
    }

    ui_navbar_create(scr, UI_TAB_TELEMETRY, tab_cb);
    return t;
}

/* ------------------------------------------------------------------ setters */

void ui_telemetry_set_position(ui_telemetry_t *t, double dd_lat, double dd_lon)
{
    if (!t) return;
    lv_label_set_text_fmt(t->pos_dd, "%.6f, %.6f | DD", dd_lat, dd_lon);
}

void ui_telemetry_set_vspeed(ui_telemetry_t *t, int value, const char *unit)
{
    if (t) lv_label_set_text_fmt(t->vspeed, "%+d %s", value, unit ? unit : "fpm");
}

void ui_telemetry_set_hdop(ui_telemetry_t *t, float hdop)
{
    if (!t) return;
    lv_label_set_text_fmt(t->hdop, "%.1f", hdop);
    lv_obj_set_style_text_color(t->hdop, hdop <= 1.5f ? UI_C_GREEN : UI_C_RED, 0);
}

void ui_telemetry_set_dop(ui_telemetry_t *t, float pdop, float vdop, int fix_type)
{
    if (!t) return;
    // Same 1.5 "good" threshold ui_telemetry_set_hdop() already uses for
    // HDOP -- PDOP/VDOP are the same dilution-of-precision quantity on
    // different axes, no reason for a different cutoff.
    lv_label_set_text_fmt(t->pdop, "%.1f", pdop);
    lv_obj_set_style_text_color(t->pdop, pdop <= 1.5f ? UI_C_GREEN : UI_C_RED, 0);
    lv_label_set_text_fmt(t->vdop, "%.1f", vdop);
    lv_obj_set_style_text_color(t->vdop, vdop <= 1.5f ? UI_C_GREEN : UI_C_RED, 0);

    const char *text = fix_type == 3 ? "3D FIX"
                      : fix_type == 2 ? "2D FIX"
                      : fix_type == 1 ? "NO FIX" : "--";
    lv_label_set_text(t->fix_type, text);
    // 2D fix means altitude (and everything derived from it -- vertical
    // speed, elevation gain) isn't trustworthy, same "flag it red" language
    // as a weak HDOP/PDOP/VDOP above.
    lv_obj_set_style_text_color(t->fix_type, fix_type == 3 ? UI_C_GREEN : UI_C_RED, 0);
}

void ui_telemetry_set_sky(ui_telemetry_t *t, const uint8_t *elevation_deg,
                          const uint16_t *azimuth_deg, const uint8_t *constellation,
                          const bool *used, int n)
{
    if (!t) return;
    // Same 5-entry bright/dim table as ui_telemetry_set_signal() -- kept as
    // its own local copy rather than shared, same reasoning as that
    // function's own copy (lv_color_hex() isn't a constant expression).
    const struct { lv_color_t bright, dim; } const_colors[5] = {
        { UI_C_GREEN,   UI_C_GREEN_DIM },    // 0 GPS
        { UI_C_GLONASS, UI_C_GLONASS_DIM },  // 1 GLONASS
        { UI_C_GALILEO, UI_C_GALILEO_DIM },  // 2 Galileo
        { UI_C_BEIDOU,  UI_C_BEIDOU_DIM },   // 3 BeiDou
        { UI_C_QZSS,    UI_C_QZSS_DIM },     // 4 QZSS
    };
    if (n > UI_TELEM_BARS) n = UI_TELEM_BARS;
    // UI_TELEM_RING_D (the rings' own diameter), not UI_TELEM_SKY_D (the
    // bigger plot box) -- a satellite right at the horizon (elevation 0)
    // should land exactly on the outer ring, not out past it in the label
    // margin.
    const float r_max = (float)UI_TELEM_RING_D / 2.0f;
    for (int i = 0; i < UI_TELEM_BARS; i++) {
        if (i >= n) {
            lv_obj_add_flag(t->sky_dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(t->sky_dots[i], LV_OBJ_FLAG_HIDDEN);

        float elev = elevation_deg[i] > 90 ? 90.0f : (float)elevation_deg[i];
        float r = r_max * (1.0f - elev / 90.0f);
        float az_rad = (float)azimuth_deg[i] * (UI_TELEM_PI / 180.0f);
        // North (az=0) is straight up -- negative y -- east (az=90) is
        // straight right, matching the N/E/S/W labels planted around the
        // ring in ui_telemetry_create().
        lv_coord_t dx = (lv_coord_t)(r * sinf(az_rad));
        lv_coord_t dy = (lv_coord_t)(-r * cosf(az_rad));
        lv_obj_align(t->sky_dots[i], LV_ALIGN_CENTER, dx, dy);

        uint8_t c = (constellation[i] < 5) ? constellation[i] : 0;
        lv_obj_set_style_bg_color(t->sky_dots[i],
                                  used[i] ? const_colors[c].bright : const_colors[c].dim, 0);
    }
}

void ui_telemetry_set_time(ui_telemetry_t *t, const char *local, const char *utc,
                           const char *tz_abbrev)
{
    if (!t) return;
    if (local)     lv_label_set_text(t->local_time, local);
    if (utc)       lv_label_set_text(t->utc_time, utc);
    if (tz_abbrev) lv_label_set_text_fmt(t->local_caption, "LOCAL | %s", tz_abbrev);
}

void ui_telemetry_set_signal(ui_telemetry_t *t, const uint8_t *snr,
                             const bool *has_snr, const uint8_t *constellation,
                             const bool *used, int n, int used_count,
                             const char *constellations)
{
    if (!t) return;
    // GPS_CONST_* order from gps.h, duplicated here rather than included --
    // see this setter's own header comment in ui_telemetry.h for why. Local
    // (not file-scope static): lv_color_hex() is a function, not a constant
    // expression, so it can't sit in a static initializer -- this just
    // builds the same 5 entries fresh each call instead, at 500ms-tick cost.
    const struct { lv_color_t bright, dim; } const_colors[5] = {
        { UI_C_GREEN,   UI_C_GREEN_DIM },    // 0 GPS
        { UI_C_GLONASS, UI_C_GLONASS_DIM },  // 1 GLONASS
        { UI_C_GALILEO, UI_C_GALILEO_DIM },  // 2 Galileo
        { UI_C_BEIDOU,  UI_C_BEIDOU_DIM },   // 3 BeiDou
        { UI_C_QZSS,    UI_C_QZSS_DIM },     // 4 QZSS
    };
    if (n > UI_TELEM_BARS) n = UI_TELEM_BARS;
    for (int i = 0; i < UI_TELEM_BARS; i++) {
        if (i >= n) {
            lv_obj_add_flag(t->bars[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(t->bars[i], LV_OBJ_FLAG_HIDDEN);

        // A bar with no SNR yet (visible but not decoding) still shows at a
        // small floor height rather than collapsing to nothing -- otherwise
        // "in view but weak" and "not there at all" look identical.
        int v = has_snr[i] ? snr[i] : 0;
        if (v > 55) v = 55;  // 55 dB-Hz is already a very strong signal; taller than that wastes bar height
        int pct = v * 100 / 55;
        if (pct < 6) pct = 6;
        lv_obj_set_height(t->bars[i], LV_PCT(pct));

        uint8_t c = (constellation[i] < 5) ? constellation[i] : 0;
        lv_obj_set_style_bg_color(t->bars[i],
                                  used[i] ? const_colors[c].bright : const_colors[c].dim, 0);
    }
    lv_label_set_text_fmt(t->signal_caption, "SIGNAL - %d IN SOLUTION", used_count);
    if (constellations) lv_label_set_text(t->constellations, constellations);
}
