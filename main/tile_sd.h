// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Scan the SD card root for tile shard files (see tile_sd.c for the naming
// convention/format) and build an in-RAM index of what's available. Call
// once, after sd_card_mount() and tile_jpeg_init(), before tile_cache_init().
// Safe to call even if the card isn't mounted or no shards are present --
// tile_sd_read() will just always return false.
void tile_sd_init(void);

// Read+decode the tile at (tile_x, tile_y, zoom) into dst (MAP_TILE_SIZE x
// MAP_TILE_SIZE, contiguous, RGB565) if it falls within one of the shard
// files found at init. Returns false (dst left untouched) for any tile
// outside every known shard, or on a read/decode error -- callers should
// fall back to synth_tile() in that case.
bool tile_sd_read(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst);
