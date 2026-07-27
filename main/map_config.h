// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Standard slippy-map tile size.
#define MAP_TILE_SIZE   256

// Fixed zoom level for v1 (pure pan/drag, no zoom UI yet). Only affects the
// procedural tile pattern's coordinate space -- swap this out (and the tile
// source) when zoom support is added later.
#define MAP_ZOOM        14

// Tile cache: worst-case on-screen tiles for a 720x1280 viewport at 256px
// tiles is 4 cols x 6 rows = 24. Add a prefetch margin ring and round up.
#define MAP_CACHE_SLOTS      64
#define MAP_PREFETCH_MARGIN  1   // extra ring of tiles loaded beyond the viewport

// Render task target rate. This is really the touch-poll rate now -- the
// dirty-check in tile_cache means idle ticks (no new touch coordinate) are
// nearly free, so polling faster catches real finger motion sooner instead
// of waiting on a slower fixed tick.
#define MAP_RENDER_FPS   60

// Placeholder fill color (RGB565) for tiles not yet generated/loaded.
#define MAP_PLACEHOLDER_RGB565  0x8410  // mid gray
