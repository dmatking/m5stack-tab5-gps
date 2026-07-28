// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Open the "tiledata" flash partition (JPEG tile index + data, see
// tools/fetch_tiles.py and the generated main/map_tiles_data.h) and set up
// the hardware JPEG decoder. Call once, after board_init(). Safe to call
// even if the partition is missing/empty -- tile_flash_read() will just
// always return false.
void tile_flash_init(void);

// Read+decode the tile at (tile_x, tile_y, zoom) into dst (MAP_TILE_SIZE x
// MAP_TILE_SIZE, contiguous, RGB565) if it falls within one of the embedded
// grids (see MAP_EMBEDDED_ZOOMS in main/map_tiles_data.h). Returns false
// (dst left untouched) for any tile outside every embedded area/zoom, or if
// the partition isn't present, or on a decode error -- callers should fall
// back to synth_tile() in that case.
bool tile_flash_read(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst);
