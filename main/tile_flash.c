// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Reads and decodes JPEG map tiles out of the "tiledata" flash partition
// (see partitions.csv). The blob is [tile index table: one (offset,length)
// uint32 pair per tile][JPEG data, packed back-to-back], generated offline
// by tools/fetch_tiles.py and flashed separately from the app image. Tile
// grid metadata for each embedded zoom level lives in the generated
// main/map_tiles_data.h. Decoding uses the ESP32-P4's hardware JPEG
// decoder, the same API already proven at real-time video rates in the
// sibling m5stack-tab5-video-stream project's player.c.

#include "tile_flash.h"
#include "map_config.h"
#include "map_tiles_data.h"

#include <assert.h>
#include <string.h>

#include "driver/jpeg_decode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

static const char *TAG = "TILE_FLASH";
static const esp_partition_t *s_part = NULL;
static jpeg_decoder_handle_t s_jpd = NULL;

// Generous headroom over the largest tile observed in testing (~25KB at
// quality 85, real z16 fetch); if a future re-fetch/quality bump produces
// bigger tiles, this needs to grow too -- fetch_tiles.py's size report
// (min/avg/max) is the source of truth to check against.
#define MAX_JPEG_TILE_BYTES  (48 * 1024)

typedef struct { uint32_t offset, length; } tile_index_entry_t;

static uint8_t *s_jpeg_in;   // reusable compressed-input buffer
static uint8_t *s_jpeg_out;  // reusable decoded-output buffer (256x256 RGB565)
static size_t s_jpeg_out_size;

void tile_flash_init(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "tiledata");
    if (!s_part) {
        ESP_LOGW(TAG, "\"tiledata\" partition not found -- real tiles disabled, using procedural fallback");
        return;
    }

    jpeg_decode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    size_t in_alloc = 0;
    s_jpeg_in = jpeg_alloc_decoder_mem(MAX_JPEG_TILE_BYTES, &in_cfg, &in_alloc);
    assert(s_jpeg_in);

    jpeg_decode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    s_jpeg_out = jpeg_alloc_decoder_mem(MAP_TILE_SIZE * MAP_TILE_SIZE * sizeof(uint16_t), &out_cfg, &s_jpeg_out_size);
    assert(s_jpeg_out);

    jpeg_decode_engine_cfg_t eng_cfg = { .timeout_ms = 1000 };
    ESP_ERROR_CHECK(jpeg_new_decoder_engine(&eng_cfg, &s_jpd));

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
    if (err != ESP_OK || entry.length == 0 || entry.length > MAX_JPEG_TILE_BYTES) {
        ESP_LOGW(TAG, "bad index entry for tile %ld,%ld z%ld: err=%d length=%u",
                 (long)tile_x, (long)tile_y, (long)zoom, err, (unsigned)entry.length);
        return false;
    }

    err = esp_partition_read(s_part, entry.offset, s_jpeg_in, entry.length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "partition read failed for tile %ld,%ld z%ld: %d",
                 (long)tile_x, (long)tile_y, (long)zoom, err);
        return false;
    }

    // rgb_order picks byte-order within each packed RGB565 pixel, not R/G/B
    // channel order (the driver header documents BGR as "small endian" and
    // RGB as "big endian") -- BGR should be the correct little-endian choice
    // to match our RISC-V/PPA pipeline, same as player.c uses. Verify on
    // first device test: if colors look swapped (sky/water reading orange),
    // flip this to JPEG_DEC_RGB_ELEMENT_ORDER_RGB.
    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_used = 0;
    err = jpeg_decoder_process(s_jpd, &dec_cfg, s_jpeg_in, entry.length, s_jpeg_out, s_jpeg_out_size, &out_used);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "jpeg decode failed for tile %ld,%ld z%ld: %d",
                 (long)tile_x, (long)tile_y, (long)zoom, err);
        return false;
    }

    memcpy(dst, s_jpeg_out, MAP_TILE_SIZE * MAP_TILE_SIZE * sizeof(uint16_t));
    return true;
}
