// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Reads and decodes JPEG map tiles out of the "tiledata" flash partition
// (see partitions.csv). The blob is [tile index table: one (offset,length)
// uint32 pair per tile][JPEG data, packed back-to-back], generated offline
// by tools/fetch_tiles.py and flashed separately from the app image. Tile
// grid metadata for each embedded zoom level lives in the generated
// main/map_tiles_data.h. Decoding itself is shared with main/tile_sd.c via
// main/tile_jpeg.c (one hardware decoder engine, one scratch buffer).

#include "tile_flash.h"
#include "map_config.h"
#include "map_tiles_data.h"
#include "tile_jpeg.h"

#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "TILE_FLASH";
static const esp_partition_t *s_part = NULL;

typedef struct { uint32_t offset, length; } tile_index_entry_t;

void tile_flash_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "tiledata");
    if (!s_part) {
        ESP_LOGW(TAG, "\"tiledata\" partition not found -- real tiles disabled, using procedural fallback");
        return;
    }

    ESP_LOGI(TAG, "tiledata partition: %u bytes, %d embedded zoom level(s)",
             (unsigned)s_part->size, MAP_EMBEDDED_ZOOM_COUNT);
    for (int i = 0; i < MAP_EMBEDDED_ZOOM_COUNT; i++) {
        const embedded_zoom_t *z = &MAP_EMBEDDED_ZOOMS[i];
        ESP_LOGI(TAG, "  zoom %ld: grid %ldx%ld @ base (%ld,%ld)",
                 (long)z->zoom, (long)z->cols, (long)z->rows, (long)z->base_tx, (long)z->base_ty);
    }
}

bool tile_flash_read(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst)
{
    if (!s_part) return false;

    const embedded_zoom_t *lvl = NULL;
    for (int i = 0; i < MAP_EMBEDDED_ZOOM_COUNT; i++) {
        if (MAP_EMBEDDED_ZOOMS[i].zoom == zoom) { lvl = &MAP_EMBEDDED_ZOOMS[i]; break; }
    }
    if (!lvl) return false; // no embedded grid at this zoom level

    int32_t col = tile_x - lvl->base_tx;
    int32_t row = tile_y - lvl->base_ty;
    if (col < 0 || col >= lvl->cols || row < 0 || row >= lvl->rows) {
        return false; // outside this level's embedded grid
    }

    uint32_t global_idx = lvl->tile_index_start + (uint32_t)(row * lvl->cols + col);
    tile_index_entry_t entry;
    esp_err_t err = esp_partition_read(s_part, global_idx * sizeof(entry), &entry, sizeof(entry));
    if (err != ESP_OK || entry.length == 0 || entry.length > tile_jpeg_input_buffer_size()) {
        ESP_LOGW(TAG, "bad index entry for tile %ld,%ld z%ld: err=%d length=%u",
                 (long)tile_x, (long)tile_y, (long)zoom, err, (unsigned)entry.length);
        return false;
    }

    err = esp_partition_read(s_part, entry.offset, tile_jpeg_input_buffer(), entry.length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "partition read failed for tile %ld,%ld z%ld: %d",
                 (long)tile_x, (long)tile_y, (long)zoom, err);
        return false;
    }

    return tile_jpeg_decode(entry.length, dst);
}
