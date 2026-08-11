// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Register the small PPA fill client used to draw the zoom buttons. Call
// once, after board_init().
void ui_overlay_init(void);

// Draw the zoom +/- buttons into the current hw back framebuffer. Call
// after a successful tile_cache_render_viewport(), before board_lcd_commit()
// -- writes go on top of whatever the map compositor just drew.
void ui_overlay_draw_zoom_buttons(void);

// Draw a GPS status bar (zoom level/fix/satellite count/lat/lon) across the
// top of the logical (portrait) screen. Same call-site contract as
// ui_overlay_draw_zoom_buttons(). Safe to call every redraw -- always
// repaints its own background first so stale characters never linger.
void ui_overlay_draw_gps_status(int32_t zoom);

// Hit-test a touch point (logical/portrait coordinates, same space as the
// map's pan math) against the zoom buttons. Returns true and sets *delta to
// +1 (zoom in) or -1 (zoom out) if (x,y) falls inside a button.
bool ui_overlay_hit_test_zoom(int16_t x, int16_t y, int *delta);

// Draw the "locate me" / home button (crosshair-in-a-circle) directly above
// the zoom buttons. Same call-site contract as ui_overlay_draw_zoom_buttons().
// follow_active switches its background to a highlight color so it's visible
// at a glance whether GPS-follow mode (see map_view.c) is currently on.
void ui_overlay_draw_home_button(bool follow_active);

// Hit-test a touch point against the home button, same coordinate space as
// ui_overlay_hit_test_zoom().
bool ui_overlay_hit_test_home(int16_t x, int16_t y);

// Draw the tab navbar across the bottom MAP_NAVBAR_H rows of the logical
// screen -- blits a real, LVGL-rendered snapshot (see navbar_snapshot.h)
// rather than rendering anything itself; Map is always the active tab
// here, so there's only ever one frame of this to show. Same call-site
// contract as ui_overlay_draw_zoom_buttons().
void ui_overlay_draw_navbar(void);

// Hit-test a touch point against the navbar. Returns true and sets
// *tab_out to which of the 5 tabs (0=Home, 1=Map, 2=Nav, 3=Telemetry,
// 4=More -- same ordering as main/ui_common.h's ui_tab_t, kept as a plain
// int rather than that LVGL-typed enum so this native-only file doesn't
// need to pull in design_ui.h/ui_common.h) if (x,y) falls inside the
// navbar's rows at all -- every touch down there resolves to *some* tab,
// same "swallow the whole strip" convention as the zoom/home buttons.
bool ui_overlay_hit_test_navbar(int16_t x, int16_t y, int *tab_out);
