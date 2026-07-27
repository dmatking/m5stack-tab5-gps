// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdint.h>

// Procedurally generate a MAP_TILE_SIZE x MAP_TILE_SIZE RGB565 tile into dst,
// deterministic per (tile_x, tile_y, zoom): a hashed background color, a
// coarse checkerboard sub-pattern, and a contrasting border, so panning (and
// zooming) can be visually verified without needing real map data -- zoom is
// folded into the hash so the pattern visibly changes when you zoom into
// territory that has no real tile data.
//
// This is the stand-in for real tile loading. The only thing a future
// SD-backed version needs to change is the call site in tile_cache.c's
// generator task -- swap this call for a fread() of a pre-converted
// /sdcard/tiles/{z}/{x}/{y}.raw file into the same dst buffer.
void synth_tile(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst);
