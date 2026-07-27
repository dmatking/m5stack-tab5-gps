// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Reads pre-converted RGB565 OSM tiles out of the "tiledata" flash
// partition (see partitions.csv). The blob is a flat row-major grid of
// MAP_TILE_GRID_COLS x MAP_TILE_GRID_ROWS tiles at a fixed zoom, generated
// offline by scratchpad/fetch_tiles.py and flashed separately from the app
// image. This is the real-imagery counterpart to tile_synth.c's procedural
// generator -- tile_cache.c's generator task tries this first and falls
// back to synth_tile() for any tile outside the embedded grid.

#include "tile_flash.h"
#include "map_config.h"

#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "TILE_FLASH";
static const esp_partition_t *s_part = NULL;

#define TILE_BYTES  (MAP_TILE_SIZE * MAP_TILE_SIZE * (int)sizeof(uint16_t))

void tile_flash_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "tiledata");
    if (!s_part) {
        ESP_LOGW(TAG, "\"tiledata\" partition not found -- real tiles disabled, using procedural fallback");
        return;
    }
    ESP_LOGI(TAG, "tiledata partition: %u bytes, grid %dx%d @ zoom %d, base (%ld,%ld)",
             (unsigned)s_part->size, MAP_TILE_GRID_COLS, MAP_TILE_GRID_ROWS, MAP_ZOOM,
             (long)MAP_TILE_BASE_TX, (long)MAP_TILE_BASE_TY);
}

bool tile_flash_read(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst)
{
    if (!s_part || zoom != MAP_ZOOM) return false;

    int32_t col = tile_x - MAP_TILE_BASE_TX;
    int32_t row = tile_y - MAP_TILE_BASE_TY;
    if (col < 0 || col >= MAP_TILE_GRID_COLS || row < 0 || row >= MAP_TILE_GRID_ROWS) {
        return false; // outside the embedded area
    }

    size_t offset = (size_t)(row * MAP_TILE_GRID_COLS + col) * TILE_BYTES;
    esp_err_t err = esp_partition_read(s_part, offset, dst, TILE_BYTES);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "partition read failed for tile %ld,%ld: %d", (long)tile_x, (long)tile_y, err);
        return false;
    }
    return true;
}
