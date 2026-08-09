// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Entry point for the generated design UI (ui_home.c/ui_map.c/ui_theme.c/
// ui_common.c -- see README.md alongside the original hand-off zip). Only
// real, wired-in-tonight change from the original: the MAP tab does NOT
// load ui_map's screen (a decorative placeholder -- schematic grid, demo
// route/track lines, no real cartography) -- it hands off to
// ui_shell_enter_map(), the existing native PPA map renderer, exactly like
// the old placeholder menu's "Map" button did. ui_map_create() is still
// called so the struct/screen exist (cheap, no images, harmless), it's
// just never shown.

#include "design_ui.h"
#include "ui_shell.h"

static ui_home_t *s_home_p;
static ui_map_t  *s_map_p;

static void tab_event_cb(lv_event_t *e)
{
    ui_tab_t tab = (ui_tab_t)(lv_uintptr_t)lv_event_get_user_data(e);
    ui_show_tab(tab);
}

void ui_init(void)
{
    ui_theme_init();
    s_home_p = ui_home_create(tab_event_cb);
    s_map_p  = ui_map_create(tab_event_cb);
    lv_screen_load(s_home_p->screen);
}

void ui_show_tab(ui_tab_t tab)
{
    switch (tab) {
    case UI_TAB_HOME:
        lv_screen_load_anim(s_home_p->screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        break;
    case UI_TAB_MAP:
        ui_shell_enter_map();
        break;
    default:
        /* NAV / TELEMETRY / MORE not ported yet — see README. */
        break;
    }
}

ui_home_t *ui_home(void) { return s_home_p; }
ui_map_t  *ui_map(void)  { return s_map_p; }
