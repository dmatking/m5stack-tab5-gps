// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Open the "tiledata" flash partition holding pre-converted RGB565 map
// tiles. Call once, after board_init(). Safe to call even if the partition
// is missing or empty -- tile_flash_read() will just always return false.
void tile_flash_init(void);

// Read the tile at (tile_x, tile_y, zoom) into dst (MAP_TILE_SIZE x
// MAP_TILE_SIZE, contiguous, RGB565) if it falls within the embedded grid
// (only present at zoom == MAP_ZOOM). Returns false (dst left untouched) for
// any tile outside the embedded area/zoom, or if the partition isn't
// present -- callers should fall back to synth_tile() in that case.
bool tile_flash_read(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst);
