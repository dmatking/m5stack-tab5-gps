#include "ui_telemetry.h"

static ui_telemetry_t s_tel;

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
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
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

    /* signal --------------------------------------------------------------- */
    lv_obj_t *sig = ui_card(body);
    lv_obj_set_width(sig, LV_PCT(100));
    lv_obj_set_flex_grow(sig, 1);
    lv_obj_set_style_pad_hor(sig, 20, 0);
    lv_obj_set_style_pad_ver(sig, 16, 0);
    ui_flex_col(sig, 10);
    lv_obj_set_flex_align(sig, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);

    lv_obj_t *shead = ui_box(sig);
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

    lv_obj_t *bars = ui_box(sig);
    lv_obj_set_size(bars, LV_PCT(100), 96);
    ui_flex_row(bars, 8);
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_END);
    // All UI_TELEM_BARS created up front, hidden until ui_telemetry_set_signal()
    // has real satellites to show -- hidden flex items don't take up row
    // space, so however many end up visible auto-fill the width via
    // flex_grow, same as if only that many had ever been created. Color/
    // height/visibility all come from real data now; no demo values.
    for (int i = 0; i < UI_TELEM_BARS; i++) {
        t->bars[i] = ui_box(bars);
        lv_obj_set_flex_grow(t->bars[i], 1);
        lv_obj_set_style_radius(t->bars[i], 3, 0);
        lv_obj_set_style_bg_opa(t->bars[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(t->bars[i], LV_OBJ_FLAG_HIDDEN);
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
