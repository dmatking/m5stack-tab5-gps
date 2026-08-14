// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Entry point for the generated design UI (ui_home.c/ui_map.c/ui_nav.c/
// ui_telemetry.c/ui_goto.c/ui_settings.c/ui_theme.c/ui_common.c -- see
// README.md alongside the original hand-off zip). Only real, wired-in
// change from the original: the MAP tab does NOT load ui_map's screen (a
// decorative placeholder -- schematic grid, demo route/track lines, no
// real cartography) -- it hands off to ui_shell_enter_map(), the existing
// native PPA map renderer, exactly like the old placeholder menu's "Map"
// button did. ui_map_create() is still called so the struct/screen exist
// (cheap, no images, harmless), it's just never shown. nav_map_cb (Nav
// screen's "Map View" footer button) goes through the same ui_show_tab()
// switch, so it gets the native handoff too, unmodified from the original.

#include "design_ui.h"
#include "ui_shell.h"

#include "app_settings.h"
#include "board_interface.h"

#include <stdio.h>

static ui_home_t      *s_home_p;
static ui_map_t       *s_map_p;
static ui_nav_t       *s_nav_p;
static ui_telemetry_t *s_tel_p;
static ui_goto_t      *s_goto_p;
static ui_settings_t  *s_set_p;
static bool            s_navigating;

static void load(lv_obj_t *screen)
{
    lv_screen_load_anim(screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

static void tab_event_cb(lv_event_t *e)
{
    ui_show_tab((ui_tab_t)(lv_uintptr_t)lv_event_get_user_data(e));
}

static void nav_map_cb(lv_event_t *e)  { LV_UNUSED(e); ui_show_tab(UI_TAB_MAP); }
static void nav_stop_cb(lv_event_t *e) { LV_UNUSED(e); ui_set_navigating(false);
                                         ui_show_goto(); }
// Parses whatever was typed into the Goto screen (ui_goto_parse() -- format-
// aware, see its own comment) into a real destination, rather than the
// screen transition alone this used to be. gps_ui_bridge.c's tick() picks
// up ui_get_destination() from here on to compute real distance/bearing/
// closure/ETA on the Nav screen every tick while ui_is_navigating().
static void goto_start_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    double lat, lon;
    // ui_goto_parse() fails (nothing to navigate to) when both fields are
    // still empty -- stay on Goto rather than switch to Nav with a stale or
    // nonexistent destination. No error toast/dialog exists to explain why
    // yet, so silently declining is the best available feedback.
    if (!ui_goto_parse(s_goto_p, &lat, &lon)) return;

    ui_set_destination(lat, lon);
    char meta[32];
    snprintf(meta, sizeof(meta), "%.4f, %.4f", lat, lon);
    ui_nav_set_destination(s_nav_p, "Custom Destination", meta);
    ui_set_navigating(true);
    ui_show_nav();
}
static void goto_cancel_cb(lv_event_t *e) { LV_UNUSED(e); ui_show_tab(UI_TAB_HOME); }

// Settings screen's "24-hour time" switch -- real (persisted, see
// app_settings.h). gps_ui_bridge.c re-formats every displayed clock on its
// own next tick, so there's no need to push a "settings changed"
// notification anywhere from here.
static void time_24h_switch_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    app_settings_set_time_24h(on);
}

// Brightness slider -- applies immediately (not just on release) so
// dragging it previews the actual result, same as most brightness sliders.
static void brightness_slider_cb(lv_event_t *e)
{
    int percent = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    app_settings_set_brightness(percent);
    board_lcd_set_brightness(percent);
    // The % label next to the slider is cosmetic (the slider's own
    // position already shows the value); keep it in sync too rather than
    // leaving it at whatever ui_settings_create()'s demo value was.
    ui_settings_set_brightness(s_set_p, percent);
}

// "Keep screen on" switch -- ui_shell.c's and map_view.c's idle timeouts
// both read app_settings_get_keep_screen_on() directly on their own next
// tick, so (like the 24-hour switch above) nothing else needs telling here.
static void screen_on_switch_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    app_settings_set_keep_screen_on(on);
}

// Row text for each of ui_goto.h's UI_COORD_* values, in order -- shared
// between Settings' Coordinate format row (below) and nowhere else; Goto's
// own format buttons have their own copy (ui_goto.c's fmt_text, a private
// static there) since they're a fixed 3-button row, not a cycling value.
static const char *coord_fmt_names[3] = { "DD MM.MMMM", "DD.DDDDDD", "DD MM SS" };

// Settings' "value" rows (chevron on the right) all report taps through
// this one callback, keyed by ui_setting_id_t. Coordinate format,
// Distance/speed, and Elevation are real (each a 2-3 way cycle, persisted
// via app_settings.h) -- Night mode/SBAS were removed outright (nothing
// real to wire, see their own removal commits) and Time zone/
// Constellations/Update rate are real read-only *displays*
// (gps_ui_bridge.c), not editable, so a tap on those is silently ignored.
static void settings_row_cb(lv_event_t *e)
{
    ui_setting_id_t id = (ui_setting_id_t)(lv_uintptr_t)lv_event_get_user_data(e);
    if (id == UI_SET_COORD_FORMAT) {
        int fmt = (app_settings_get_coord_format() + 1) % 3;
        app_settings_set_coord_format(fmt);
        ui_settings_set_value(s_set_p, UI_SET_COORD_FORMAT, coord_fmt_names[fmt]);
        // Deliberately NOT touching Goto's own format toggle here -- Settings
        // only seeds Goto's format at boot (see ui_init() below); once a
        // session has visited Goto, only its own 3-button toggle controls it
        // from there, so coordinate entry can always be overridden per-entry
        // regardless of the Settings default.
    } else if (id == UI_SET_UNITS) {
        bool km = !app_settings_get_distance_km();
        app_settings_set_distance_km(km);
        ui_settings_set_value(s_set_p, UI_SET_UNITS, km ? "km / km/h" : "mi / mph");
    } else if (id == UI_SET_ELEVATION) {
        bool m = !app_settings_get_elevation_m();
        app_settings_set_elevation_m(m);
        ui_settings_set_value(s_set_p, UI_SET_ELEVATION, m ? "meters" : "feet");
    }
}

void ui_init(void)
{
    ui_theme_init();

    s_home_p = ui_home_create(tab_event_cb);
    s_map_p  = ui_map_create(tab_event_cb);
    s_nav_p  = ui_nav_create(tab_event_cb);
    s_tel_p  = ui_telemetry_create(tab_event_cb);
    s_goto_p = ui_goto_create(tab_event_cb);
    s_set_p  = ui_settings_create(tab_event_cb);

    ui_nav_set_buttons(s_nav_p, nav_map_cb, nav_stop_cb);
    ui_goto_set_start_cb(s_goto_p, goto_start_cb, NULL);
    ui_goto_set_cancel_cb(s_goto_p, goto_cancel_cb, NULL);

    // Sync each switch/slider to its real persisted value -- ui_settings_create()
    // itself always creates them at their own demo values, same "creation-time
    // placeholder, corrected right after" pattern throughout this function.
    ui_settings_set_time_24h(s_set_p, app_settings_get_time_24h());
    ui_settings_set_time_24h_cb(s_set_p, time_24h_switch_cb);

    int brightness = app_settings_get_brightness();
    ui_settings_set_brightness(s_set_p, brightness);
    board_lcd_set_brightness(brightness);
    ui_settings_set_brightness_cb(s_set_p, brightness_slider_cb);

    ui_settings_set_screen_on(s_set_p, app_settings_get_keep_screen_on());
    ui_settings_set_screen_on_cb(s_set_p, screen_on_switch_cb);

    int coord_fmt = app_settings_get_coord_format();
    ui_settings_set_value(s_set_p, UI_SET_COORD_FORMAT, coord_fmt_names[coord_fmt]);

    bool dist_km = app_settings_get_distance_km();
    ui_settings_set_value(s_set_p, UI_SET_UNITS, dist_km ? "km / km/h" : "mi / mph");
    bool elev_m = app_settings_get_elevation_m();
    ui_settings_set_value(s_set_p, UI_SET_ELEVATION, elev_m ? "meters" : "feet");

    ui_settings_set_row_cb(s_set_p, settings_row_cb);
    // Seeds Goto's format toggle only -- its own 3-button row stays freely
    // overridable per-entry from here on, see settings_row_cb()'s comment.
    ui_goto_set_format(s_goto_p, (ui_coord_fmt_t)coord_fmt);

    ui_set_navigating(false);
    // Deliberately not loading a screen here -- ui_shell.c controls the
    // splash-then-Home sequencing at boot (loading Home here raced against
    // its own splash screen and caused a visible flash: default LVGL screen
    // -> Home -> splash -> Home).
}

void ui_show_tab(ui_tab_t tab)
{
    switch (tab) {
    case UI_TAB_HOME:      load(s_home_p->screen); break;
    case UI_TAB_MAP:       ui_shell_enter_map();   break;
    case UI_TAB_NAV:       s_navigating ? ui_show_nav() : ui_show_goto(); break;
    case UI_TAB_TELEMETRY: load(s_tel_p->screen);  break;
    case UI_TAB_MORE:      load(s_set_p->screen);  break;
    default: break;
    }
}

void ui_show_goto(void) { load(s_goto_p->screen); }
void ui_show_nav(void)  { load(s_nav_p->screen); }

void ui_set_navigating(bool navigating)
{
    s_navigating = navigating;
    ui_map_show_navigation(s_map_p, navigating);
}

bool ui_is_navigating(void) { return s_navigating; }

static double s_dest_lat, s_dest_lon;
static bool   s_dest_valid;

void ui_set_destination(double lat, double lon)
{
    s_dest_lat = lat;
    s_dest_lon = lon;
    s_dest_valid = true;
}

bool ui_get_destination(double *lat, double *lon)
{
    if (!s_dest_valid) return false;
    if (lat) *lat = s_dest_lat;
    if (lon) *lon = s_dest_lon;
    return true;
}

ui_home_t      *ui_home(void)      { return s_home_p; }
ui_map_t       *ui_map(void)       { return s_map_p; }
ui_nav_t       *ui_nav(void)       { return s_nav_p; }
ui_telemetry_t *ui_telemetry(void) { return s_tel_p; }
ui_goto_t      *ui_goto(void)      { return s_goto_p; }
ui_settings_t  *ui_settings(void)  { return s_set_p; }
