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

// Draw a GPS status bar (fix/satellite count/lat/lon) across the top of the
// logical (landscape) screen. Same call-site contract as
// ui_overlay_draw_zoom_buttons(). Safe to call every redraw -- always
// repaints its own background first so stale characters never linger.
void ui_overlay_draw_gps_status(void);

// Hit-test a touch point (logical/landscape coordinates, same space as the
// map's pan math) against the zoom buttons. Returns true and sets *delta to
// +1 (zoom in) or -1 (zoom out) if (x,y) falls inside a button.
bool ui_overlay_hit_test_zoom(int16_t x, int16_t y, int *delta);
