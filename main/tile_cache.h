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
// Returns false (and does nothing) if neither the viewport nor the zoom has
// moved and no tile finished loading since the last call -- there's nothing
// new to show, so skip the redraw entirely rather than re-blitting an
// unchanged screen. When it returns true, the caller should call
// board_lcd_commit() to flip.
bool tile_cache_render_viewport(int32_t pan_x, int32_t pan_y, int32_t zoom);

// Force the next tile_cache_render_viewport() call to redraw even if pan/
// zoom haven't moved and no tile just finished loading -- for overlays
// (e.g. the GPS status bar) whose own content changed independently of the
// map. Uses the same epoch counter background tile completions already bump.
void tile_cache_mark_dirty(void);
