// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Standard slippy-map tile size.
#define MAP_TILE_SIZE   256

// Fixed zoom level for v1 (pure pan/drag, no zoom UI yet).
#define MAP_ZOOM        16

// Embedded real-tile grid (see main/tile_flash.c, scratchpad/fetch_tiles.py).
// Centered on 32.8896614814945,-97.34129452988256. Panning outside this
// range falls back to the procedural pattern from tile_synth.c.
#define MAP_TILE_BASE_TX     15043
#define MAP_TILE_BASE_TY     26416
#define MAP_TILE_GRID_COLS   8
#define MAP_TILE_GRID_ROWS   10

// Tile cache: worst-case on-screen tiles for a 1280x720 logical viewport at
// 256px tiles is 6 cols x 4 rows = 24 (same total either orientation, just
// swapped axes). Add a prefetch margin ring and round up.
#define MAP_CACHE_SLOTS      64
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
