// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Set up LVGL and show the splash screen, then the main shell (Map/Settings
// buttons). Call once, after board_init() and every native subsystem the
// map screen depends on (touch_init(), sd_card_mount(), gps_init(),
// tile_jpeg_init()/tile_sd_init()/tile_cache_init()) -- picking "Map" from
// the shell hands off straight into the existing map_view_start() task,
// so all of that has to already be ready by the time a tap can reach it.
// Returns immediately; the splash/menu/map flow all continue on their own
// tasks from here.
void ui_shell_start(void);

// Called from the Map screen (main/map_view.c, on its swipe-up-from-bottom
// exit gesture) to hand the panel back to LVGL and show the main menu
// again. Caller is expected to delete its own task immediately after
// calling this; it doesn't return control to map_view.c.
void ui_shell_return_to_menu(void);

// Switches straight back to the main menu screen -- for callers that are
// already LVGL screens themselves (e.g. main/mockup_viewer.c) and don't
// need the native-renderer handoff ui_shell_return_to_menu() does. Takes
// the LVGL port lock itself.
void ui_shell_show_main_menu(void);
