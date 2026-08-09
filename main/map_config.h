// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Standard slippy-map tile size.
#define MAP_TILE_SIZE   256

// Preferred starting zoom level (used to pick the initial view -- see
// map_view.c). Embedded real-tile imagery now covers whichever zoom levels
// are listed in the generated main/map_tiles_data.h (MAP_EMBEDDED_ZOOMS[]);
// any zoom in [MAP_MIN_ZOOM, MAP_MAX_ZOOM] without an embedded grid shows
// the procedural fallback pattern from tile_synth.c instead.
#define MAP_ZOOM        16
#define MAP_MIN_ZOOM    10
// z19 was tried and dropped (2026-08-08): ArcGIS's "Recreate Missing Tiles"
// pass came back byte-for-byte identical to the original export -- the
// blank tiles at z19 are genuine gaps in the underlying Esri source
// imagery (confirmed by decoding the raw tile at a known-covered address
// and finding it uniformly transparent straight out of Pro's own cache),
// not something a recreate pass can fix. z18 is the deepest zoom level
// that's actually reliable, so it's the cap.
#define MAP_MAX_ZOOM    18

// Double-tap-to-zoom: max duration/movement for a press to count as a
// "tap", and max time/distance between two taps to pair them into a
// double-tap. Heuristic UX values -- expect to retune after trying them on
// real hardware.
#define MAP_TAP_MAX_DURATION_US     300000
#define MAP_TAP_MAX_MOVEMENT_PX     15
#define MAP_DOUBLE_TAP_WINDOW_US    400000
#define MAP_DOUBLE_TAP_RADIUS_PX    40

// Colors below are RGB565 conversions of main/ui_theme.h's palette (that
// file is LVGL/RGB888-only, lv_color_hex() macros -- can't be #included
// here without pulling LVGL into the native tile-render path, so the
// values are ported by hand instead; see that file if the theme changes).
// Keeps the Map screen's own chrome consistent with the real Home screen
// rather than looking like a bolted-on different app -- these used to be
// plain ad-hoc near-black/pure-green/pure-red before the design UI existed
// to match at all.
#define MAP_THEME_CARD_RGB565    0x08A3  // ui_theme.h UI_C_CARD    0x0D141C
#define MAP_THEME_NAVBAR_RGB565  0x0862  // ui_theme.h UI_C_NAVBAR  0x080C11
#define MAP_THEME_BG_RGB565      0x0041  // ui_theme.h UI_C_BG      0x05080C
#define MAP_THEME_BLUE_RGB565    0x2C1D  // ui_theme.h UI_C_BLUE    0x2F80ED
#define MAP_THEME_GREEN_RGB565   0x3E85  // ui_theme.h UI_C_GREEN   0x3ED12A
#define MAP_THEME_RED_RGB565     0xE32D  // ui_theme.h UI_C_RED     0xE5646E
#define MAP_THEME_ICON_RGB565    0x6BD1  // ui_theme.h UI_C_ICON    0x6B7A8A -- inactive navbar tab color
#define MAP_THEME_DIVIDER_RGB565 0x10E5  // ui_theme.h UI_C_DIVIDER 0x141C25 -- navbar's top border line

// On-screen zoom +/- buttons (see main/ui_overlay.c), stacked in the
// bottom-right corner of the logical viewport. Zoom-in is above zoom-out.
#define MAP_BUTTON_SIZE            80
#define MAP_BUTTON_MARGIN          20
#define MAP_BUTTON_GAP             12
#define MAP_BUTTON_BG_RGB565       MAP_THEME_CARD_RGB565
#define MAP_BUTTON_GLYPH_RGB565    0xFFFF  // white
#define MAP_BUTTON_GLYPH_THICKNESS 8       // px, the "+"/"-" bar thickness

// "Locate me" / home button (crosshair-in-a-circle icon), stacked directly
// above the zoom buttons in the same column -- see main/ui_overlay.c for the
// icon drawing and main/map_view.c for the follow-GPS behavior it triggers.
// Same button footprint (MAP_BUTTON_SIZE) as the zoom buttons; only the
// icon geometry inside it is different.
#define MAP_HOME_RING_RADIUS       18  // px, outer radius of the circle outline
#define MAP_HOME_RING_THICKNESS    3   // px
#define MAP_HOME_DOT_RADIUS        4   // px, solid center dot
#define MAP_HOME_TICK_GAP          3   // px, gap between the ring and each tick
#define MAP_HOME_TICK_LEN          9   // px
#define MAP_HOME_TICK_THICKNESS    5   // px
#define MAP_HOME_BG_RGB565         MAP_THEME_CARD_RGB565  // matches the zoom buttons when inactive
#define MAP_HOME_BG_ACTIVE_RGB565  MAP_THEME_BLUE_RGB565  // highlight while follow mode is on
#define MAP_HOME_GLYPH_RGB565      0xFFFF  // white

// Tile cache: worst-case on-screen tiles for a 1280x720 logical viewport at
// 256px tiles is 6 cols x 4 rows = 24 (same total either orientation, just
// swapped axes). With adjacent-zoom-level prefetch (see tile_cache.c), up to
// three zoom levels' worth of viewport+margin tiles can be wanted at once
// (~48 each with the margin ring), so slots are sized well above one level's
// worth to avoid evicting tiles that are still wanted.
#define MAP_CACHE_SLOTS      128
#define MAP_PREFETCH_MARGIN  1   // extra ring of tiles loaded beyond the viewport

// Portrait-native app: logical space is the same orientation as the
// physical panel (720x1280), so placement in tile_cache.c/ui_overlay.c is a
// plain, unrotated block copy -- no coordinate transform of any kind.
//
// This used to be 1280x720 (landscape), with the map composited sideways
// and placed into the native framebuffer via a 90deg CCW coordinate
// transform, matching a matching pre-rotation baked into the tile pixel
// data itself (holding the Tab5's keyboard dock rotated was the intended
// use). That placement-side rotation turned out to be the actual source of
// a real, reproduced-on-hardware tile flicker during panning (position-
// dependent, survived VSYNC-gating and cross-core-atomics fixes, gone
// immediately when placement went identity) -- and unnecessary in the
// first place, since the physical act of holding the device rotated plus
// pre-rotating tile content offline is already a complete transform on its
// own; adding a second, software placement-side rotation on top was
// redundant. tools/fetch_tiles.py no longer pre-rotates tile content
// (--rotate defaults to 0) to match.
#define MAP_LOGICAL_W   720
#define MAP_LOGICAL_H   1280

// Render task target rate. This is really the touch-poll rate now -- the
// dirty-check in tile_cache means idle ticks (no new touch coordinate) are
// nearly free, so polling faster catches real finger motion sooner instead
// of waiting on a slower fixed tick.
#define MAP_RENDER_FPS   60

// Placeholder fill color (RGB565) for tiles not yet generated/loaded.
#define MAP_PLACEHOLDER_RGB565  0x8410  // mid gray

// Idle time (touch-only -- GPS/background activity doesn't count) before
// the backlight turns off to save power. The first tap after that just
// wakes the screen back up; it's consumed (no pan/zoom/tap side effect),
// same convention as a zoom-button touch. 0 disables the timeout.
#define MAP_SCREEN_TIMEOUT_US  180000000  // 3min

// Height (logical rows, bottom of screen) of the tab navbar drawn by
// main/ui_overlay.c -- mirrors main/ui_common.c's real LVGL navbar (same
// height, same palette, same 5-tab set: Home/Map/Nav/Telemetry/More) so the
// Map screen doesn't look like a separate, disconnected app bolted onto the
// real design UI. Tapping a tab other than Map hands the panel back to
// LVGL and switches straight to it (main/map_view.c -> ui_shell.c's
// ui_shell_return_to_tab()) -- this replaced an earlier swipe-up-from-the-
// bottom-edge gesture that only went to Home; the navbar is a strictly
// better replacement (visible affordance, reaches all 5 tabs, not just
// Home), and its hit region fully covers where that gesture used to start,
// so the old code was removed rather than left dead alongside this.
// Shared with main/tile_cache.c, which (like MAP_STATUS_BAR_H below) clips
// tile compositing to never write into this strip.
#define MAP_NAVBAR_H  116  // matches ui_theme.h's UI_NAVBAR_H

// Height (logical rows, top of screen) of the GPS status bar drawn by
// main/ui_overlay.c. Shared with main/tile_cache.c, which clips tile
// compositing to never write into this strip at all -- previously tiles and
// the status bar both drew into the same rows every frame (tiles first,
// overlay on top), and with double-buffering and no VSYNC-gated buffer
// reuse, the DSI scan-out could occasionally read a buffer mid-composite,
// after tiles were written but before the overlay redrew on top, flashing
// raw tile pixels into the status bar for one frame. Excluding the strip
// from tile compositing entirely removes that window regardless of timing.
#define MAP_STATUS_BAR_H  32
