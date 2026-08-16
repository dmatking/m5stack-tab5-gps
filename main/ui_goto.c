#include "ui_goto.h"
#include <stdlib.h>
#include <string.h>

static ui_goto_t s_goto;

static const char *fmt_text[3] = { "DD MM.MMMM", "DD.DDDDDD", "DD MM SS" };
static const char *keys[12] = { "1","2","3","4","5","6","7","8","9",
                                "N / S","0","DEL" };

/* ------------------------------------------------------------------ helpers */

static void refresh_fields(ui_goto_t *g)
{
    bool lat = (g->active == UI_GOTO_FIELD_LAT);
    lv_obj_set_style_border_color(g->lat_card, lat ? UI_C_BLUE : UI_C_BORDER, 0);
    lv_obj_set_style_border_color(g->lon_card, lat ? UI_C_BORDER : UI_C_BLUE, 0);
    lv_obj_set_style_text_color(g->lat_value, lat ? UI_C_TEXT : UI_C_TEXT_2, 0);
    lv_obj_set_style_text_color(g->lon_value, lat ? UI_C_TEXT_2 : UI_C_TEXT, 0);
    lv_label_set_text(g->lat_value, g->lat_buf);
    lv_label_set_text(g->lon_value, g->lon_buf);
    lv_label_set_text(g->lat_hemi, g->lat_north ? "N" : "S");
    lv_label_set_text(g->lon_hemi, g->lon_east ? "E" : "W");
}

static void field_cb(lv_event_t *e)
{
    ui_goto_t *g = &s_goto;
    g->active = (ui_goto_field_t)(lv_uintptr_t)lv_event_get_user_data(e);
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
    } else if (idx == 9) {                 /* hemisphere toggle */
        if (g->active == UI_GOTO_FIELD_LAT) g->lat_north = !g->lat_north;
        else                                g->lon_east  = !g->lon_east;
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
    lv_obj_t *seg = ui_card(body);
    // "Saved waypoints" has no event handler at all -- tapping it does
    // nothing right now (no waypoint storage exists yet). See project notes.
    ui_mark_placeholder(seg);
    lv_obj_set_width(seg, LV_PCT(100));
    lv_obj_set_height(seg, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(seg, 6, 0);
    ui_flex_row(seg, 8);

    g->tab_entry = ui_box(seg);
    lv_obj_set_size(g->tab_entry, LV_SIZE_CONTENT, 64);
    lv_obj_set_flex_grow(g->tab_entry, 1);
    lv_obj_set_style_radius(g->tab_entry, 12, 0);
    lv_obj_set_style_bg_color(g->tab_entry, UI_C_BLUE_BTN, 0);
    lv_obj_set_style_bg_opa(g->tab_entry, LV_OPA_COVER, 0);
    lv_obj_t *te = ui_label(g->tab_entry, "Enter coordinates", ui_font.semi_s, UI_C_TEXT);
    lv_obj_center(te);

    g->tab_saved = ui_box(seg);
    lv_obj_set_size(g->tab_saved, LV_SIZE_CONTENT, 64);
    lv_obj_set_flex_grow(g->tab_saved, 1);
    lv_obj_set_style_radius(g->tab_saved, 12, 0);
    lv_obj_add_flag(g->tab_saved, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *ts = ui_label(g->tab_saved, "Saved waypoints", ui_font.s, UI_C_MUTED);
    lv_obj_center(ts);

    /* coordinate format ---------------------------------------------------- */
    lv_obj_t *fmts = ui_box(body);
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
    lv_obj_t *rec = ui_card(body);
    // Fixed demo waypoint ("Gemini Bridges"), and clickable in name only --
    // no event handler either. See project notes.
    ui_mark_placeholder(rec);
    lv_obj_set_width(rec, LV_PCT(100));
    lv_obj_set_height(rec, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(rec, 20, 0);
    lv_obj_set_style_pad_ver(rec, 12, 0);
    lv_obj_add_flag(rec, LV_OBJ_FLAG_CLICKABLE);
    ui_flex_row(rec, 14);
    lv_obj_set_flex_align(rec, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *rleft = ui_box(rec);
    lv_obj_set_size(rleft, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_row(rleft, 14);
    lv_obj_set_flex_align(rleft, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ui_label(rleft, LV_SYMBOL_GPS, ui_font.semi_s, UI_C_GREEN);
    lv_obj_t *rtext = ui_box(rleft);
    lv_obj_set_size(rtext, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    ui_flex_col(rtext, 2);
    g->recent_name = ui_label(rtext, "Gemini Bridges", ui_font.semi_s, UI_C_TEXT);
    // "|" not "\xC2\xB7" (·) -- see ui_home.c's ± comment; same missing-
    // glyph issue, different character, same fix (ASCII substitute).
    g->recent_meta = ui_label(rtext, "last used | 6.4 mi | 094\xC2\xB0",
                              ui_font.xs, UI_C_MUTED);
    ui_label(rec, "Load " LV_SYMBOL_RIGHT, ui_font.s, UI_C_BLUE);

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
    lv_obj_set_size(br, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(br, 2);
    g->btn_start = ui_button(br, "Start Navigation", UI_C_BLUE_BTN, UI_C_TEXT,
                             false, UI_C_BLUE, 92);

    ui_navbar_create(scr, UI_TAB_NAV, tab_cb);
    refresh_fields(g);
    return g;
}

/* ------------------------------------------------------------------ setters */

void ui_goto_set_format(ui_goto_t *g, ui_coord_fmt_t fmt)
{
    if (!g) return;
    g->fmt = fmt;
    for (int i = 0; i < 3; i++) {
        bool on = (i == (int)fmt);
        lv_obj_set_style_border_color(g->fmt_btn[i], on ? UI_C_BLUE : UI_C_BORDER, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(g->fmt_btn[i], 0),
                                    on ? UI_C_BLUE : UI_C_MUTED, 0);
    }
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

void ui_goto_set_recent(ui_goto_t *g, const char *name, float dist_mi, int brg)
{
    if (!g) return;
    if (name) lv_label_set_text(g->recent_name, name);
    lv_label_set_text_fmt(g->recent_meta, "last used | %.1f mi | %03d\xC2\xB0",
                          dist_mi, ((brg % 360) + 360) % 360);
}

const char *ui_goto_get_lat(ui_goto_t *g) { return g ? g->lat_buf : ""; }
const char *ui_goto_get_lon(ui_goto_t *g) { return g ? g->lon_buf : ""; }

// Digit-string -> decimal-degrees magnitude, format- and field-aware (lat's
// degree field is 2 digits, max 90; lon's is 3, max 180 -- same convention
// already used throughout this app's DDM display, e.g. gps_ui_bridge.c's
// format_ddm()). key_cb() only ever appends bare digit characters (no
// punctuation), so this is the first place any of these typed digits get
// interpreted as an actual coordinate at all -- there was no pre-existing
// convention to match, this is a new one, documented here:
//   DDM: <deg><MM><MMMM>   -- e.g. lat "32596420" -> 32 deg, 59.6420 min
//   DD:  <deg><DDDDDD>     -- e.g. lat "32902057"  -> 32.902057 deg
//   DMS: <deg><MM><SS>     -- e.g. lat "325934"    -> 32 deg 59 min 34 sec
// Fewer digits than the format needs are treated as trailing zeros (a
// partially-typed field still parses to *something* sane); extra digits
// beyond that are ignored. Result is clamped to the field's valid range
// (0-90 lat, 0-180 lon) rather than rejected -- simplest well-defined
// behavior for a fat-fingered entry, no error-dialog UI exists to reject
// through yet.
static double parse_field(const char *digits, ui_coord_fmt_t fmt, bool is_lat)
{
    int deg_w = is_lat ? 2 : 3;
    char buf[16] = { 0 };
    size_t n = strlen(digits);
    if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
    memcpy(buf, digits, n);
    // Pad with '0' out to the format's full field width so a short/empty
    // entry still parses instead of reading past the end of what was typed.
    int total_w = deg_w + (fmt == UI_COORD_DMS ? 4 : 6); // DDM/DD both 6, DMS 4
    for (size_t i = n; i < (size_t)total_w && i < sizeof(buf) - 1; i++) buf[i] = '0';

    char tmp[8];
    memcpy(tmp, buf, (size_t)deg_w); tmp[deg_w] = '\0';
    double deg = atof(tmp);

    double value;
    if (fmt == UI_COORD_DD) {
        memcpy(tmp, buf + deg_w, 6); tmp[6] = '\0';
        value = deg + atof(tmp) / 1e6;
    } else if (fmt == UI_COORD_DDM) {
        memcpy(tmp, buf + deg_w, 2); tmp[2] = '\0';
        double min_whole = atof(tmp);
        memcpy(tmp, buf + deg_w + 2, 4); tmp[4] = '\0';
        double min_frac = atof(tmp) / 1e4;
        value = deg + (min_whole + min_frac) / 60.0;
    } else { // UI_COORD_DMS
        memcpy(tmp, buf + deg_w, 2); tmp[2] = '\0';
        double min = atof(tmp);
        memcpy(tmp, buf + deg_w + 2, 2); tmp[2] = '\0';
        double sec = atof(tmp);
        value = deg + min / 60.0 + sec / 3600.0;
    }

    double max = is_lat ? 90.0 : 180.0;
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
