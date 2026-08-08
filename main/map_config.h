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
#define MAP_MAX_ZOOM    19

// Double-tap-to-zoom: max duration/movement for a press to count as a
// "tap", and max time/distance between two taps to pair them into a
// double-tap. Heuristic UX values -- expect to retune after trying them on
// real hardware.
#define MAP_TAP_MAX_DURATION_US     300000
#define MAP_TAP_MAX_MOVEMENT_PX     15
#define MAP_DOUBLE_TAP_WINDOW_US    400000
#define MAP_DOUBLE_TAP_RADIUS_PX    40

// On-screen zoom +/- buttons (see main/ui_overlay.c), stacked in the
// bottom-right corner of the logical viewport. Zoom-in is above zoom-out.
#define MAP_BUTTON_SIZE            80
#define MAP_BUTTON_MARGIN          20
#define MAP_BUTTON_GAP             12
#define MAP_BUTTON_BG_RGB565       0x2104  // near-black
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
#define MAP_HOME_BG_RGB565         0x2104  // near-black, matches the zoom buttons when inactive
#define MAP_HOME_BG_ACTIVE_RGB565  0x045F  // blue highlight while follow mode is on
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
