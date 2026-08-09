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
static void goto_start_cb(lv_event_t *e)  { LV_UNUSED(e); ui_set_navigating(true);
                                            ui_show_nav(); }
static void goto_cancel_cb(lv_event_t *e) { LV_UNUSED(e); ui_show_tab(UI_TAB_HOME); }

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

ui_home_t      *ui_home(void)      { return s_home_p; }
ui_map_t       *ui_map(void)       { return s_map_p; }
ui_nav_t       *ui_nav(void)       { return s_nav_p; }
ui_telemetry_t *ui_telemetry(void) { return s_tel_p; }
ui_goto_t      *ui_goto(void)      { return s_goto_p; }
ui_settings_t  *ui_settings(void)  { return s_set_p; }
