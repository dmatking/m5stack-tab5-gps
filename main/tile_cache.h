// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Allocate the tile slot pool, register PPA clients, and start the
// background tile-generator task. Call once, after board_init().
void tile_cache_init(void);

// Composite the full screen for the viewport at the given zoom whose
// top-left world-pixel corner is (pan_x, pan_y), writing directly into the
// current hardware back framebuffer via PPA blits. Tiles not yet generated
// are filled with a placeholder color instead of blocking. Also requests
// generation of any visible-or-nearby tiles that aren't cached yet.
//
// dragging: true while a drag gesture is in progress -- suspends the
// adjacent-zoom (+-1) prefetch (current-zoom viewport requests still happen
// normally). Sustained panning can otherwise inject new distinct tile
// requests faster than the generator (rate-limited, and slower per-tile
// than the old flash reads) can drain them; with three zoom levels' worth
// of requests competing for a fixed-size slot pool, that backlog can fill
// entirely with stale (no-longer-visible) work and starve the tiles
// actually on screen of a slot to generate into at all -- see
// generator_task()'s stale-request skip for the other half of this fix.
bool tile_cache_render_viewport(int32_t pan_x, int32_t pan_y, int32_t zoom, bool dragging);

// Force the next tile_cache_render_viewport() call to redraw even if pan/
// zoom haven't moved and no tile just finished loading -- for overlays
// (e.g. the GPS status bar) whose own content changed independently of the
// map. Uses the same epoch counter background tile completions already bump.
void tile_cache_mark_dirty(void);
