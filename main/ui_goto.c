#include "ui_goto.h"
#include <stdlib.h>
#include <string.h>

static ui_goto_t s_goto;

static const char *fmt_text[3] = { "DD MM.MMMM", "DD.DDDDDD", "DD MM SS" };
// idx 9 used to be a dedicated "N / S" hemisphere-toggle key; hemisphere is
// now toggled by tapping the N/S/E/W badge next to each field directly
// (coord_field()'s hemi_badge_cb), which freed this slot for a real decimal
// point -- see parse_field()'s own comment on why DD.DDDDDD needed one.
static const char *keys[12] = { "1","2","3","4","5","6","7","8","9",
                                ".","0","DEL" };

/* ------------------------------------------------------------------ helpers */

// Live, format-aware rendering of whatever's been typed so far -- e.g. lat
// digits "3296" under DD MM SS shows as "32\xC2\xB0 96" while still
// mid-entry. This is what parse_field() (further down) has always
// interpreted the raw digit buffer AS for DDM/DMS, it just never showed up
// anywhere before: switching the format buttons looked like it did
// nothing. DD.DDDDDD is handled separately below -- it's free-form now
// (see parse_field()'s comment on why), so there's nothing to segment,
// just the typed string plus a degree sign. DDM/DMS's segment widths here
// must match parse_field()'s layout exactly; kept as a second hardcoded
// `deg_w` rather than sharing one, same as that function already
// duplicates it from nowhere else -- there's no third place that needs it.
static void format_entry_preview(const char *digits, ui_coord_fmt_t fmt, bool is_lat,
                                 char *out, size_t out_size)
{
    if (digits[0] == '\0') {
        lv_snprintf(out, out_size, "--");
        return;
    }

    if (fmt == UI_COORD_DD) {
        // Free-form -- show exactly what's been typed (digits and at most
        // one '.' from the keypad's decimal key), degree sign appended.
        // No fixed-width degree field here, unlike DDM/DMS below -- see
        // parse_field()'s matching comment for why.
        lv_snprintf(out, out_size, "%s\xC2\xB0", digits);
        return;
    }

    // DD.DDDDDD returned above -- everything below is DDM/DMS, unchanged:
    // still the original fixed-width positional convention, degree field
    // included (a longitude under 100\xC2\xB0 still needs a typed leading
    // zero to fill it, same as before this fix). Only DD.DDDDDD's
    // degree/fraction split needed a real decimal key -- DDM/DMS's own
    // internal split (whole vs. fractional minutes) is a fixed 2-then-4
    // digit convention with no equivalent ambiguity to fix.
    int deg_w = is_lat ? 2 : 3;
    int seg_w[3];
    const char *seg_punct[3];
    int nseg;
    if (fmt == UI_COORD_DDM) {
        seg_w[0] = deg_w; seg_punct[0] = "\xC2\xB0 ";
        seg_w[1] = 2;     seg_punct[1] = ".";
        seg_w[2] = 4;     seg_punct[2] = "'";
        nseg = 3;
    } else { // UI_COORD_DMS
        seg_w[0] = deg_w; seg_punct[0] = "\xC2\xB0 ";
        seg_w[1] = 2;     seg_punct[1] = "' ";
        seg_w[2] = 2;     seg_punct[2] = "\"";
        nseg = 3;
    }

    size_t n = strlen(digits);
    size_t pos = 0, o = 0;
    for (int s = 0; s < nseg && pos < n && o + 1 < out_size; s++) {
        int avail = (int)(n - pos);
        int take = avail < seg_w[s] ? avail : seg_w[s];
        for (int i = 0; i < take && o + 1 < out_size; i++) out[o++] = digits[pos + (size_t)i];
        pos += (size_t)take;
        // Punctuation for this segment only lands once it's fully typed --
        // a segment still mid-entry (e.g. one digit into a 2-digit minutes
        // field) shows bare, no trailing symbol yet.
        if (take == seg_w[s]) {
            size_t plen = strlen(seg_punct[s]);
            if (o + plen < out_size) { memcpy(out + o, seg_punct[s], plen); o += plen; }
        }
    }
    out[o] = '\0';
}

// Forward-declared -- parse_field() (near ui_goto_parse(), at the bottom)
// is this file's single source of truth for interpreting a typed buffer as
// a coordinate magnitude, but refresh_fields() below needs the *unclamped*
// value to know whether to flag a field as out of range, before that
// function is defined.
static double parse_field_raw(const char *digits, ui_coord_fmt_t fmt, bool is_lat);

// True while the field is either still empty (nothing typed, nothing to
// flag) or unambiguously within [0, max] for its axis; false once it's
// gone past -- e.g. a typed latitude magnitude over 90. refresh_fields()
// colors a false field red, and ui_goto_parse() refuses to navigate while
// either one is -- see ui_goto_parse()'s own comment for why silent
// clamping (which parse_field() also still does, as a last-resort
// fallback) isn't good enough for a field that's actually driving where
// "Start Navigation" sends you.
static bool field_in_range(const char *digits, ui_coord_fmt_t fmt, bool is_lat)
{
    if (digits[0] == '\0') return true;
    double max = is_lat ? 90.0 : 180.0;
    return parse_field_raw(digits, fmt, is_lat) <= max;
}

static void refresh_fields(ui_goto_t *g)
{
    bool lat = (g->active == UI_GOTO_FIELD_LAT);
    bool lat_ok = field_in_range(g->lat_buf, g->fmt, true);
    bool lon_ok = field_in_range(g->lon_buf, g->fmt, false);

    lv_obj_set_style_border_color(g->lat_card,
        !lat_ok ? UI_C_RED : (lat ? UI_C_BLUE : UI_C_BORDER), 0);
    lv_obj_set_style_border_color(g->lon_card,
        !lon_ok ? UI_C_RED : (lat ? UI_C_BORDER : UI_C_BLUE), 0);
    lv_obj_set_style_text_color(g->lat_value,
        !lat_ok ? UI_C_RED : (lat ? UI_C_TEXT : UI_C_TEXT_2), 0);
    lv_obj_set_style_text_color(g->lon_value,
        !lon_ok ? UI_C_RED : (lat ? UI_C_TEXT_2 : UI_C_TEXT), 0);

    char lat_disp[32], lon_disp[32];
    format_entry_preview(g->lat_buf, g->fmt, true,  lat_disp, sizeof(lat_disp));
    format_entry_preview(g->lon_buf, g->fmt, false, lon_disp, sizeof(lon_disp));
    lv_label_set_text(g->lat_value, lat_disp);
    lv_label_set_text(g->lon_value, lon_disp);

    lv_label_set_text(g->lat_hemi, g->lat_north ? "N" : "S");
    lv_label_set_text(g->lon_hemi, g->lon_east ? "E" : "W");
}

/* Saved-list row actions. Set from design_ui.c, which owns the store. */
static void (*s_saved_go_cb)(int index);
static void (*s_saved_del_cb)(int index);
// Forward-declared: ui_goto_create() attaches this to the recent-waypoint
// card (always index 0, the newest) before its real definition further
// down, where the saved-list rows also use it.
static void saved_row_cb(lv_event_t *e);

static void seg_cb(lv_event_t *e)
{
    ui_goto_set_tab(&s_goto, (bool)(lv_uintptr_t)lv_event_get_user_data(e));
}

static void field_cb(lv_event_t *e)
{
    ui_goto_t *g = &s_goto;
    g->active = (ui_goto_field_t)(lv_uintptr_t)lv_event_get_user_data(e);
    refresh_fields(g);
}

// Tapping the N/S/E/W badge directly flips that field's hemisphere (and
// makes it the active field, same as tapping anywhere else on its card) --
// replaces the old dedicated keypad key, see keys[]'s own comment on why.
static void hemi_badge_cb(lv_event_t *e)
{
    ui_goto_t *g = &s_goto;
    ui_goto_field_t which = (ui_goto_field_t)(lv_uintptr_t)lv_event_get_user_data(e);
    g->active = which;
    if (which == UI_GOTO_FIELD_LAT) g->lat_north = !g->lat_north;
    else                            g->lon_east  = !g->lon_east;
    refresh_fields(g);
}

static void fmt_cb(lv_event_t *e)
{
    ui_goto_set_format(&s_goto,
        (ui_coord_fmt_t)(lv_uintptr_t)lv_event_get_user_data(e));
}

static void key_cb(lv_event_t *e)
{
    ui_goto_t *g = &s_goto;
    int idx = (int)(lv_uintptr_t)lv_event_get_user_data(e);
    char *buf = (g->active == UI_GOTO_FIELD_LAT) ? g->lat_buf : g->lon_buf;
    size_t len = strlen(buf);

    if (idx == 11) {                       /* DEL */
        if (len) buf[len - 1] = '\0';
    } else if (idx == 9) {                 /* decimal point -- DD format only */
        // DDM/DMS's decimal points (minutes' fraction) stay positionally
        // implied, fixed-width -- only DD.DDDDDD's degree/fraction split is
        // free-form enough to need an explicit key. Silently ignored
        // otherwise, same as DEL on an already-empty field: nothing to do,
        // not an error. At most one '.' -- a second tap is also a no-op.
        if (g->fmt == UI_COORD_DD && !strchr(buf, '.') &&
            len < sizeof(g->lat_buf) - 1) {
            buf[len] = '.'; buf[len + 1] = '\0';
        }
    } else {
        const char *d = (idx == 10) ? "0" : keys[idx];
        if (len < sizeof(g->lat_buf) - 1) { buf[len] = d[0]; buf[len + 1] = '\0'; }
    }
    refresh_fields(g);
}

static lv_obj_t *coord_field(lv_obj_t *parent, const char *caption,
                             const char *value, const char *hemi, bool active,
                             ui_goto_field_t which, lv_obj_t **out_value,
                             lv_obj_t **out_hemi)
{
    lv_obj_t *c = ui_card(parent);
    lv_obj_set_width(c, LV_PCT(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(c, 20, 0);
    lv_obj_set_style_pad_ver(c, 14, 0);
    lv_obj_set_style_border_color(c, active ? UI_C_BLUE : UI_C_BORDER, 0);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(c, field_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)which);
    ui_flex_row(c, 12);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = ui_box(c);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(left, 2);
    ui_caption(left, caption);
    *out_value = ui_label(left, value, ui_font.num_m,
                          active ? UI_C_TEXT : UI_C_TEXT_2);

    lv_obj_t *badge = ui_box(c);
    lv_obj_set_size(badge, 76, 56);
    lv_obj_set_style_radius(badge, 12, 0);
    lv_obj_set_style_bg_color(badge, UI_C_CARD_ALT, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    // Already CLICKABLE from ui_box()'s default constructor -- previously
    // left with no handler, which under LVGL's deepest-clickable-wins hit
    // testing meant tapping exactly on the badge silently swallowed the
    // tap instead of reaching the card's own field_cb. Now it does
    // something instead of nothing.
    lv_obj_add_event_cb(badge, hemi_badge_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)which);
    *out_hemi = ui_label(badge, hemi, ui_font.semi_m, UI_C_GREEN);
    lv_obj_center(*out_hemi);
    return c;
}

/* ------------------------------------------------------------------- create */

ui_goto_t *ui_goto_create(lv_event_cb_t tab_cb)
{
    ui_goto_t *g = &s_goto;
    lv_memzero(g, sizeof(*g));
    // lat_buf/lon_buf start empty (lv_memzero already does that) -- used to
    // be pre-filled with demo text ("32 59.6420"), fine while this screen
    // was decorative, a real bug now that key_cb() appends to whatever's
    // already in the buffer rather than replacing it: the first real digit
    // typed would land after the leftover demo text instead of starting
    // clean.
    g->lat_north = true;
    g->lon_east  = false;
    g->fmt       = UI_COORD_DDM;

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_size(scr, UI_SCREEN_W, UI_SCREEN_H);
    lv_obj_set_style_bg_color(scr, UI_C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_flex_col(scr, 0);
    g->screen = scr;

    ui_status_create(scr, &g->status);

    lv_obj_t *body = ui_box(scr);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_hor(body, UI_PAD_SIDE, 0);
    lv_obj_set_style_pad_bottom(body, 16, 0);
    ui_flex_col(body, 12);

    /* entry / saved segmented control -------------------------------------- */
    // No ui_mark_placeholder() -- both halves are real now: they switch
    // between the coordinate keypad and the saved-waypoint list below.
    lv_obj_t *seg = ui_card(body);
    lv_obj_set_width(seg, LV_PCT(100));
    lv_obj_set_height(seg, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(seg, 6, 0);
    ui_flex_row(seg, 8);

    g->tab_entry = ui_box(seg);
    lv_obj_set_size(g->tab_entry, LV_SIZE_CONTENT, 64);
    lv_obj_set_flex_grow(g->tab_entry, 1);
    lv_obj_set_style_radius(g->tab_entry, 12, 0);
    lv_obj_add_flag(g->tab_entry, LV_OBJ_FLAG_CLICKABLE);   // was display-only
    lv_obj_add_event_cb(g->tab_entry, seg_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)false);
    g->tab_entry_label = ui_label(g->tab_entry, "Enter coordinates", ui_font.semi_s, UI_C_TEXT);
    lv_obj_center(g->tab_entry_label);

    g->tab_saved = ui_box(seg);
    lv_obj_set_size(g->tab_saved, LV_SIZE_CONTENT, 64);
    lv_obj_set_flex_grow(g->tab_saved, 1);
    lv_obj_set_style_radius(g->tab_saved, 12, 0);
    lv_obj_add_flag(g->tab_saved, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g->tab_saved, seg_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)true);
    g->tab_saved_label = ui_label(g->tab_saved, "Saved waypoints", ui_font.s, UI_C_MUTED);
    lv_obj_center(g->tab_saved_label);

    /* coordinate format ---------------------------------------------------- */
    lv_obj_t *fmts = ui_box(body);
    g->fmts = fmts;
    lv_obj_set_size(fmts, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(fmts, 8);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = ui_box(fmts);
        lv_obj_set_size(b, LV_SIZE_CONTENT, 58);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, i == 0 ? UI_C_BLUE : UI_C_BORDER, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, fmt_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
        lv_obj_t *l = ui_label(b, fmt_text[i], ui_font.xs,
                               i == 0 ? UI_C_BLUE : UI_C_MUTED);
        lv_obj_center(l);
        g->fmt_btn[i] = b;
    }

    /* lat / lon ------------------------------------------------------------ */
    g->lat_card = coord_field(body, "LATITUDE", g->lat_buf, "N", true,
                              UI_GOTO_FIELD_LAT, &g->lat_value, &g->lat_hemi);
    g->lon_card = coord_field(body, "LONGITUDE", g->lon_buf, "W", false,
                              UI_GOTO_FIELD_LON, &g->lon_value, &g->lon_hemi);

    /* keypad --------------------------------------------------------------- */
    lv_obj_t *pad = ui_box(body);
    g->pad = pad;
    lv_obj_set_size(pad, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pad, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(pad, 10, 0);
    lv_obj_set_style_pad_row(pad, 10, 0);
    for (int i = 0; i < 12; i++) {
        lv_obj_t *k = ui_card(pad);
        lv_obj_set_size(k, 220, 74);
        lv_obj_set_style_radius(k, 14, 0);
        lv_obj_set_style_pad_all(k, 0, 0);
        lv_obj_add_flag(k, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(k, key_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)i);
        bool digit = (i < 9) || (i == 10);
        lv_obj_t *l = ui_label(k, keys[i], digit ? ui_font.semi_m : ui_font.s,
                               digit ? UI_C_TEXT : UI_C_MUTED);
        lv_obj_center(l);
    }

    /* recent waypoint ------------------------------------------------------- */
    // No ui_mark_placeholder() -- real now: shows the most recently saved
    // waypoint (waypoints_get(0), design_ui.c's ui_goto_refresh_saved()) and
    // navigates to it on tap, same as a row in the saved list. Hidden
    // entirely with an empty store (ui_goto_set_recent(g, NULL, NULL)) --
    // there's nothing "recent" to show yet. Distance/bearing dropped from
    // the old demo text ("last used | 6.4 mi | 094°") -- that needs a live
    // position tick this card doesn't have; just name and coordinates.
    lv_obj_t *rec = ui_card(body);
    g->rec = rec;
    lv_obj_set_width(rec, LV_PCT(100));
    lv_obj_set_height(rec, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(rec, 20, 0);
    lv_obj_set_style_pad_ver(rec, 12, 0);
    lv_obj_add_flag(rec, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(rec, saved_row_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)0);
    ui_flex_row(rec, 14);
    lv_obj_set_flex_align(rec, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *rleft = ui_box(rec);
    lv_obj_remove_flag(rleft, LV_OBJ_FLAG_CLICKABLE);   // let the card underneath keep the tap
    lv_obj_set_size(rleft, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(rleft, 14);
    lv_obj_set_flex_align(rleft, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(rleft, LV_SYMBOL_GPS, ui_font.semi_s, UI_C_GREEN);
    lv_obj_t *rtext = ui_box(rleft);
    lv_obj_remove_flag(rtext, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(rtext, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(rtext, 2);
    g->recent_name = ui_label(rtext, "WPT 001", ui_font.semi_s, UI_C_TEXT);
    g->recent_meta = ui_label(rtext, "32.9021, -97.3261", ui_font.xs, UI_C_MUTED);
    ui_label(rec, "Load " LV_SYMBOL_RIGHT, ui_font.s, UI_C_BLUE);

    /* saved pane: the waypoint list ---------------------------------------- */
    // The first scrollable container in this app -- every screen (and
    // ui_box() itself) strips LV_OBJ_FLAG_SCROLLABLE, so it has to be put
    // back deliberately here, along with an explicit vertical-only scroll
    // direction. flex_grow gives it a bounded height, which is what makes
    // scrolling mean anything: without it the container would just grow to
    // fit every row and never scroll.
    g->saved_list = ui_box(body);
    lv_obj_remove_flag(g->saved_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(g->saved_list, LV_PCT(100));
    lv_obj_set_flex_grow(g->saved_list, 1);
    lv_obj_add_flag(g->saved_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(g->saved_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g->saved_list, LV_SCROLLBAR_MODE_AUTO);
    ui_flex_col(g->saved_list, 8);
    lv_obj_add_flag(g->saved_list, LV_OBJ_FLAG_HIDDEN);   // entry pane is the default

    // Shown instead of rows when the store is empty, so the pane never just
    // looks broken.
    g->saved_empty = ui_label(body, "No saved waypoints yet\n"
                                    "Use Mark Position on the Home screen.",
                              ui_font.s, UI_C_DIM);
    lv_obj_set_width(g->saved_empty, LV_PCT(100));
    lv_label_set_long_mode(g->saved_empty, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g->saved_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(g->saved_empty, LV_OBJ_FLAG_HIDDEN);

    /* footer ---------------------------------------------------------------- */
    lv_obj_t *spacer = ui_box(body);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);

    lv_obj_t *btns = ui_box(body);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(btns, 12);
    lv_obj_t *bl = ui_box(btns);
    lv_obj_set_size(bl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bl, 1);
    g->btn_cancel = ui_button(bl, "Cancel", UI_C_CARD, UI_C_MUTED, true,
                              UI_C_BORDER, 92);
    lv_obj_t *br = ui_box(btns);
    g->btn_start_wrap = br;
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(br, 2);
    g->btn_start = ui_button(br, "Start Navigation", UI_C_BLUE_BTN, UI_C_TEXT,
                             false, UI_C_BLUE, 92);

    ui_navbar_create(scr, UI_TAB_NAV, tab_cb);
    refresh_fields(g);
    // Entry pane is the default -- also does the initial tab styling and
    // hides the saved pane / Start Navigation consistently, rather than
    // relying on the creation-time flags above staying in sync with it.
    ui_goto_set_tab(g, false);
    return g;
}

/* ------------------------------------------------------------------ setters */

// Re-derives every bit of visibility that depends on saved_mode/has_recent/
// list-contents together, rather than each caller (tab switched, list
// refreshed, recent card updated) trying to patch just its own piece and
// risk fighting one of the others.
static void update_saved_visibility(ui_goto_t *g)
{
    bool empty = (lv_obj_get_child_count(g->saved_list) == 0);
    bool show_list  = g->saved_mode && !empty;
    bool show_empty = g->saved_mode && empty;
    if (show_list)  lv_obj_remove_flag(g->saved_list, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(g->saved_list, LV_OBJ_FLAG_HIDDEN);
    if (show_empty) lv_obj_remove_flag(g->saved_empty, LV_OBJ_FLAG_HIDDEN);
    else            lv_obj_add_flag(g->saved_empty, LV_OBJ_FLAG_HIDDEN);

    // rec (the recent-waypoint card) belongs only to the entry pane, same
    // as fmts/lat_card/lon_card/pad, but ALSO needs to stay hidden there
    // when there's nothing recent to show -- two independent conditions,
    // which is why it's handled here instead of in set_tab()'s flat list.
    bool show_rec = !g->saved_mode && g->has_recent;
    if (show_rec) lv_obj_remove_flag(g->rec, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(g->rec, LV_OBJ_FLAG_HIDDEN);
}

void ui_goto_set_tab(ui_goto_t *g, bool saved)
{
    if (!g) return;
    g->saved_mode = saved;

    // Entry-group widgets: direct, flat children of `body`, individually
    // hidden -- see ui_goto.h's own comment on why there's no wrapping
    // pane container to toggle instead.
    lv_obj_t *entry_widgets[] = { g->fmts, g->lat_card, g->lon_card, g->pad };
    for (size_t i = 0; i < sizeof(entry_widgets) / sizeof(entry_widgets[0]); i++) {
        if (saved) lv_obj_add_flag(entry_widgets[i], LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_remove_flag(entry_widgets[i], LV_OBJ_FLAG_HIDDEN);
    }
    update_saved_visibility(g);

    // "Start Navigation" only belongs to the entry pane -- on the saved
    // pane the coordinate fields are empty, so ui_goto_parse() would
    // refuse and the button would sit there looking live while doing
    // nothing. Rows are tap-to-navigate instead. Cancel stays on both (it
    // just goes Home) and widens to fill the row.
    if (g->btn_start_wrap) {
        if (saved) lv_obj_add_flag(g->btn_start_wrap, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_remove_flag(g->btn_start_wrap, LV_OBJ_FLAG_HIDDEN);
    }

    // Selected tab is a filled blue pill, the other is bare. Matches how
    // refresh_fields() re-styles the lat/lon cards on focus.
    lv_obj_set_style_bg_color(g->tab_entry, UI_C_BLUE_BTN, 0);
    lv_obj_set_style_bg_opa(g->tab_entry, saved ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g->tab_entry_label, saved ? UI_C_MUTED : UI_C_TEXT, 0);
    lv_obj_set_style_text_font(g->tab_entry_label, saved ? ui_font.s : ui_font.semi_s, 0);

    lv_obj_set_style_bg_color(g->tab_saved, UI_C_BLUE_BTN, 0);
    lv_obj_set_style_bg_opa(g->tab_saved, saved ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(g->tab_saved_label, saved ? UI_C_TEXT : UI_C_MUTED, 0);
    lv_obj_set_style_text_font(g->tab_saved_label, saved ? ui_font.semi_s : ui_font.s, 0);
}

void ui_goto_set_saved_cbs(ui_goto_t *g, void (*go_cb)(int), void (*del_cb)(int))
{
    LV_UNUSED(g);
    s_saved_go_cb  = go_cb;
    s_saved_del_cb = del_cb;
}

static void saved_row_cb(lv_event_t *e)
{
    if (s_saved_go_cb) s_saved_go_cb((int)(lv_uintptr_t)lv_event_get_user_data(e));
}

/* Delete confirmation -- same scrim+card pattern as Home's Reset Trip
 * dialog (ui_home.c:108-191), reproduced here rather than shared because
 * it's a small, self-contained bit of UI with no obvious common home yet.
 * Only one can ever be open at a time (only a trash tap opens it), so
 * file-static state is enough. The actual store mutation stays out of this
 * file -- see del_confirm_yes_cb(), same layering as saved_row_cb above. */
static lv_obj_t *s_del_confirm;
static int       s_del_pending_index;

static void del_confirm_close(void)
{
    if (s_del_confirm) {
        lv_obj_delete(s_del_confirm);
        s_del_confirm = NULL;
    }
}

static void del_confirm_no_cb(lv_event_t *e)
{
    (void)e;
    del_confirm_close();
}

static void del_confirm_yes_cb(lv_event_t *e)
{
    (void)e;
    int index = s_del_pending_index;
    del_confirm_close();
    if (s_saved_del_cb) s_saved_del_cb(index);
}

static void del_btn_cb(lv_event_t *e)
{
    ui_goto_t *g = &s_goto;
    if (s_del_confirm) return; // already open
    s_del_pending_index = (int)(lv_uintptr_t)lv_event_get_user_data(e);

    // FLOATING here is load-bearing, same reason as ui_home.c's reset
    // dialog: g->screen lays out body+navbar in a flex column, so without
    // it this scrim would become a 3rd flex item after the navbar instead
    // of covering it.
    lv_obj_t *scrim = ui_box(g->screen);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_pos(scrim, 0, 0);
    lv_obj_set_size(scrim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_60, 0);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
    s_del_confirm = scrim;

    lv_obj_t *card = ui_card(scrim);
    lv_obj_set_width(card, LV_PCT(80));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 20, 0);
    ui_flex_col(card, 16);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    ui_label(card, "Delete this waypoint?", ui_font.semi_m, UI_C_TEXT);
    lv_obj_t *desc = ui_label(card, "This can't be undone.", ui_font.s, UI_C_MUTED);
    lv_obj_set_width(desc, LV_PCT(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btns = ui_box(card);
    lv_obj_set_size(btns, LV_PCT(100), LV_SIZE_CONTENT);
    ui_flex_row(btns, 12);
    lv_obj_t *bl = ui_box(btns);
    lv_obj_set_size(bl, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(bl, 1);
    lv_obj_t *no_btn = ui_button(bl, "Cancel", UI_C_CARD, UI_C_MUTED, true, UI_C_BORDER, 64);
    lv_obj_add_event_cb(no_btn, del_confirm_no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *br = ui_box(btns);
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(br, 1);
    lv_obj_t *yes_btn = ui_button(br, "Delete", UI_C_CARD, UI_C_RED, true, lv_color_hex(0x63303A), 64);
    lv_obj_add_event_cb(yes_btn, del_confirm_yes_cb, LV_EVENT_CLICKED, NULL);
}

void ui_goto_saved_begin(ui_goto_t *g)
{
    if (g) lv_obj_clean(g->saved_list);
}

void ui_goto_saved_add(ui_goto_t *g, int index, const char *name, const char *meta)
{
    if (!g || !name) return;

    // Explicit height, never LV_SIZE_CONTENT: a size-to-content flex row
    // inside a fixed/bounded parent is the arrangement that silently
    // mispositions and clips in this app (it's what hid the whole status
    // bar on four screens), and fixed rows also make the scroll extent
    // predictable.
    lv_obj_t *row = ui_card(g->saved_list);
    lv_obj_set_size(row, LV_PCT(100), 88);
    lv_obj_set_style_pad_hor(row, 16, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, saved_row_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)index);
    ui_flex_row(row, 12);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *left = ui_box(row);
    // Stripped so the row underneath keeps the tap: hit-testing picks the
    // DEEPEST clickable object, and ui_box() leaves CLICKABLE set. (Labels
    // are safe -- lv_label's own constructor removes the flag.)
    lv_obj_remove_flag(left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(left, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(left, 1);
    ui_flex_col(left, 2);
    ui_label(left, name, ui_font.semi_s, UI_C_TEXT);
    if (meta) ui_label(left, meta, ui_font.xs, UI_C_MUTED);

    // Keeps CLICKABLE on purpose -- being deeper than the row, it wins the
    // hit test, so trash taps never fall through to "navigate here".
    lv_obj_t *del = ui_box(row);
    lv_obj_set_size(del, 60, 60);
    lv_obj_set_style_radius(del, 12, 0);
    lv_obj_set_style_bg_color(del, UI_C_CARD_ALT, 0);
    lv_obj_set_style_bg_opa(del, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(del, del_btn_cb, LV_EVENT_CLICKED, (void *)(lv_uintptr_t)index);
    lv_obj_t *dl = ui_label(del, LV_SYMBOL_TRASH, ui_font.s, UI_C_RED);
    lv_obj_center(dl);
}

void ui_goto_saved_end(ui_goto_t *g)
{
    if (!g) return;
    update_saved_visibility(g);
}

// Removes any '.' from a buffer in place -- DDM/DMS's fixed-width segment
// walk (format_entry_preview()/parse_field()) assumes pure digits, but
// DD.DDDDDD's decimal key can leave one behind if the user switches format
// mid-entry. Without this, that '.' would just be copied/atof()'d as if it
// were a digit, miscounting every segment after it.
static void strip_dot(char *buf)
{
    char *w = buf;
    for (char *r = buf; *r; r++) {
        if (*r != '.') *w++ = *r;
    }
    *w = '\0';
}

void ui_goto_set_format(ui_goto_t *g, ui_coord_fmt_t fmt)
{
    if (!g) return;
    g->fmt = fmt;
    if (fmt != UI_COORD_DD) {
        strip_dot(g->lat_buf);
        strip_dot(g->lon_buf);
    }
    for (int i = 0; i < 3; i++) {
        bool on = (i == (int)fmt);
        lv_obj_set_style_border_color(g->fmt_btn[i], on ? UI_C_BLUE : UI_C_BORDER, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g->fmt_btn[i], 0),
                                    on ? UI_C_BLUE : UI_C_MUTED, 0);
    }
    // Re-render whatever's already typed under the new format's punctuation
    // -- previously this only restyled the buttons themselves, so picking a
    // different format looked like it did nothing to the value underneath.
    refresh_fields(g);
}

void ui_goto_set_field(ui_goto_t *g, ui_goto_field_t field)
{
    if (!g) return;
    g->active = field;
    refresh_fields(g);
}

void ui_goto_set_coords(ui_goto_t *g, const char *lat, const char *lon)
{
    if (!g) return;
    if (lat) lv_strlcpy(g->lat_buf, lat, sizeof(g->lat_buf));
    if (lon) lv_strlcpy(g->lon_buf, lon, sizeof(g->lon_buf));
    refresh_fields(g);
}

void ui_goto_set_recent(ui_goto_t *g, const char *name, const char *meta)
{
    if (!g) return;
    g->has_recent = (name != NULL);
    if (name) {
        lv_label_set_text(g->recent_name, name);
        lv_label_set_text(g->recent_meta, meta ? meta : "");
    }
    update_saved_visibility(g);
}

const char *ui_goto_get_lat(ui_goto_t *g) { return g ? g->lat_buf : ""; }
const char *ui_goto_get_lon(ui_goto_t *g) { return g ? g->lon_buf : ""; }

// Digit-string -> decimal-degrees magnitude, format- and field-aware (lat's
// degree field is 2 digits, max 90; lon's is 3, max 180 -- same convention
// already used throughout this app's DDM display, e.g. gps_ui_bridge.c's
// format_ddm()). key_cb() only ever appends bare digit characters (or, for
// DD.DDDDDD, at most one literal '.') -- this is the first place any of
// these get interpreted as an actual coordinate at all:
//   DDM: <deg><MM><MMMM>   -- e.g. lat "32596420" -> 32 deg, 59.6420 min
//   DD:  free-form, atof() -- e.g. lat "32.9021"   -> 32.9021 deg
//   DMS: <deg><MM><SS>     -- e.g. lat "325934"    -> 32 deg 59 min 34 sec
// DD used to be fixed-width like the other two (<deg><DDDDDD>, a 2-or-3
// digit degree field same as DDM/DMS's), which meant a longitude under
// 100 needed a typed leading zero ("097...") before its degrees field
// "finished" and the fraction could start -- otherwise the 3rd digit typed
// silently became part of degrees instead of the fraction, e.g. lat/lon
// mixed up which digit was which. DD.DDDDDD's whole point is typing an
// ordinary decimal number, so it gets to just be one now -- no leading
// zero, no fixed width, degrees is however many digits precede the '.'.
// DDM/DMS keep the fixed-width convention below (their own internal
// splits -- whole/fractional minutes, minutes/seconds -- are narrow,
// well-known-width fields, not the trap the degree field was).
// Fewer digits than DDM/DMS need are treated as trailing zeros (a
// partially-typed field still parses to *something* sane); extra digits
// beyond that are ignored. Unclamped -- field_in_range() (above) is what
// actually flags an out-of-range magnitude; parse_field() below just
// clamps it as a last-resort fallback for the rare path that skips that
// check (e.g. a stray call before the UI's ever refreshed once).
static double parse_field_raw(const char *digits, ui_coord_fmt_t fmt, bool is_lat)
{
    if (fmt == UI_COORD_DD) return atof(digits);

    int deg_w = is_lat ? 2 : 3;
    char buf[16] = { 0 };
    size_t n = strlen(digits);
    if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
    memcpy(buf, digits, n);
    // Pad with '0' out to the format's full field width so a short/empty
    // entry still parses instead of reading past the end of what was typed.
    int total_w = deg_w + (fmt == UI_COORD_DMS ? 4 : 6); // DDM 6, DMS 4
    for (size_t i = n; i < (size_t)total_w && i < sizeof(buf) - 1; i++) buf[i] = '0';

    char tmp[8];
    memcpy(tmp, buf, (size_t)deg_w); tmp[deg_w] = '\0';
    double deg = atof(tmp);

    if (fmt == UI_COORD_DDM) {
        memcpy(tmp, buf + deg_w, 2); tmp[2] = '\0';
        double min_whole = atof(tmp);
        memcpy(tmp, buf + deg_w + 2, 4); tmp[4] = '\0';
        double min_frac = atof(tmp) / 1e4;
        return deg + (min_whole + min_frac) / 60.0;
    }
    // UI_COORD_DMS
    memcpy(tmp, buf + deg_w, 2); tmp[2] = '\0';
    double min = atof(tmp);
    memcpy(tmp, buf + deg_w + 2, 2); tmp[2] = '\0';
    double sec = atof(tmp);
    return deg + min / 60.0 + sec / 3600.0;
}

// Result is clamped to the field's valid range (0-90 lat, 0-180 lon)
// rather than rejected -- but see ui_goto_parse() below, which now checks
// field_in_range() *before* calling this, so in practice Start Navigation
// never actually reaches an unclamped-then-silently-coerced value; this
// clamp is a defensive fallback, not the real validation anymore.
static double parse_field(const char *digits, ui_coord_fmt_t fmt, bool is_lat)
{
    double max = is_lat ? 90.0 : 180.0;
    double value = parse_field_raw(digits, fmt, is_lat);
    if (value < 0.0) value = 0.0;
    if (value > max)  value = max;
    return value;
}

bool ui_goto_parse(ui_goto_t *g, double *out_lat, double *out_lon)
{
    if (!g || !out_lat || !out_lon) return false;
    // Nothing typed in either field at all -- parse_field() would happily
    // treat that as all-zero digits and return a "valid" (0, 0), silently
    // sending Start Navigation to the Gulf of Guinea. Refuse instead;
    // there's no error-dialog UI to explain a bad entry through yet, so a
    // no-op (stay on this screen) is the safest failure mode available.
    if (g->lat_buf[0] == '\0' && g->lon_buf[0] == '\0') return false;
    // Same refusal for a magnitude that's actually out of range (lat > 90,
    // lon > 180) -- these fields are already showing red by the time
    // Start Navigation could be tapped (refresh_fields()'s field_in_range()
    // check), so this isn't the user's only signal something's wrong, but
    // it's what stops a fat-fingered "99.85" from silently becoming a
    // clamped-to-90 destination with no indication it wasn't what was typed.
    if (!field_in_range(g->lat_buf, g->fmt, true))  return false;
    if (!field_in_range(g->lon_buf, g->fmt, false)) return false;
    double lat = parse_field(g->lat_buf, g->fmt, true);
    double lon = parse_field(g->lon_buf, g->fmt, false);
    *out_lat = g->lat_north ? lat : -lat;
    *out_lon = g->lon_east  ? lon : -lon;
    return true;
}

void ui_goto_set_start_cb(ui_goto_t *g, lv_event_cb_t cb, void *user_data)
{
    if (g && cb) lv_obj_add_event_cb(g->btn_start, cb, LV_EVENT_CLICKED, user_data);
}

void ui_goto_set_cancel_cb(ui_goto_t *g, lv_event_cb_t cb, void *user_data)
{
    if (g && cb) lv_obj_add_event_cb(g->btn_cancel, cb, LV_EVENT_CLICKED, user_data);
}
