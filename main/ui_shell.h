// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

// Set up LVGL and show the splash screen, then the real design UI
// (main/design_ui.c -- Home screen + tab bar). Call once, after
// board_init() and every native subsystem the map screen depends on
// (touch_init(), sd_card_mount(), gps_init(), tile_jpeg_init()/
// tile_sd_init()/tile_cache_init()) -- picking "Map" from the tab bar
// hands off straight into the existing map_view_start() task, so all of
// that has to already be ready by the time a tap can reach it. Returns
// immediately; the splash/UI/map flow all continue on their own tasks
// from here.
void ui_shell_start(void);

// Called from design_ui.c's ui_show_tab() when the MAP tab is selected --
// stops LVGL and hands the panel to the native map renderer. See its
// definition in ui_shell.c for why ui_map's own screen isn't used.
void ui_shell_enter_map(void);

// Called from the Map screen (main/map_view.c, on a navbar tab tap -- see
// main/ui_overlay.c's ui_overlay_draw_navbar()) to hand the panel back to
// LVGL and switch to the given tab. tab_index uses the same 0..4 ordering
// as ui_overlay.h's ui_overlay_draw_navbar() (0=Home, 1=Map, 2=Nav,
// 3=Telemetry, 4=More) -- never call this with 1 (Map); map_view.c's own
// navbar handler already skips it, since the Map screen doesn't hand off
// to itself. Caller is expected to delete its own task immediately after
// calling this; it doesn't return control to map_view.c.
void ui_shell_return_to_tab(int tab_index);

// Switches straight back to the Home screen -- for callers that are
// already LVGL screens themselves and don't need the native-renderer
// handoff ui_shell_return_to_tab() does. Takes the LVGL port lock itself.
void ui_shell_show_main_menu(void);

// True while the Map screen's native renderer owns the panel (LVGL
// stopped, see ui_shell_enter_map()) -- lv_screen_active() still returns
// whatever LVGL screen was loaded before the handoff, not what's actually
// on the panel, so callers that snapshot the active screen (main/fb_capture.c)
// need to check this first rather than capturing stale/wrong content.
bool ui_shell_map_active(void);
