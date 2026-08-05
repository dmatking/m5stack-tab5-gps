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

// Tile cache: worst-case on-screen tiles for a 1280x720 logical viewport at
// 256px tiles is 6 cols x 4 rows = 24 (same total either orientation, just
// swapped axes). With adjacent-zoom-level prefetch (see tile_cache.c), up to
// three zoom levels' worth of viewport+margin tiles can be wanted at once
// (~48 each with the margin ring), so slots are sized well above one level's
// worth to avoid evicting tiles that are still wanted.
#define MAP_CACHE_SLOTS      128
#define MAP_PREFETCH_MARGIN  1   // extra ring of tiles loaded beyond the viewport

// The physical panel is portrait (720x1280), but the Tab5's keyboard dock
// makes landscape the natural hold -- so the map is composited in this
// logical (landscape) space and placed into the native framebuffer via a
// coordinate transform in tile_cache.c, matching a 90deg CCW pre-rotation
// baked into the tile pixel data itself (tools/fetch_tiles.py --rotate 90).
// No PPA rotation happens at runtime; only placement math changes.
#define MAP_LOGICAL_W   1280
#define MAP_LOGICAL_H   720

// Render task target rate. This is really the touch-poll rate now -- the
// dirty-check in tile_cache means idle ticks (no new touch coordinate) are
// nearly free, so polling faster catches real finger motion sooner instead
// of waiting on a slower fixed tick.
#define MAP_RENDER_FPS   60

// Placeholder fill color (RGB565) for tiles not yet generated/loaded.
#define MAP_PLACEHOLDER_RGB565  0x8410  // mid gray

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
