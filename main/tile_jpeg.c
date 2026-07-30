// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Shared hardware JPEG decoder engine + DMA-capable scratch input buffer,
// extracted out of main/tile_flash.c so a second tile source (main/tile_sd.c)
// doesn't need its own duplicate ~48KB input buffer + decoder engine handle.
// jpeg_decoder_process() takes an internal mutex and blocks to completion
// (confirmed in the ESP-IDF driver source), so sharing one engine across
// callers is safe as long as calls stay sequential -- true here since both
// callers only ever run from tile_cache.c's single generator_task, which
// fully completes one tile (read -> decode) before dequeuing the next.

#include "tile_jpeg.h"
#include "map_config.h"

#include <assert.h>
#include <string.h>

#include "driver/jpeg_decode.h"
#include "esp_log.h"

static const char *TAG = "TILE_JPEG";

// Generous headroom over the largest tile observed in testing (~25KB at
// quality 85, real z16 fetch); if a future re-fetch/quality bump (or a much
// larger/denser SD dataset) produces bigger tiles, this needs to grow too --
// fetch_tiles.py's size report (min/avg/max) is the source of truth to check
// against. This ceiling now bounds SD-sourced tiles too, not just flash ones.
#define MAX_JPEG_TILE_BYTES  (48 * 1024)

static jpeg_decoder_handle_t s_jpd = NULL;
static uint8_t *s_jpeg_in;
static size_t s_jpeg_in_size;
static uint8_t *s_jpeg_out;  // reusable decoded-output buffer (256x256 RGB565)
static size_t s_jpeg_out_size;

void tile_jpeg_init(void)
{
    jpeg_decode_memory_alloc_cfg_t in_cfg = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
    s_jpeg_in = jpeg_alloc_decoder_mem(MAX_JPEG_TILE_BYTES, &in_cfg, &s_jpeg_in_size);
    assert(s_jpeg_in);

    jpeg_decode_memory_alloc_cfg_t out_cfg = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
    s_jpeg_out = jpeg_alloc_decoder_mem(MAP_TILE_SIZE * MAP_TILE_SIZE * sizeof(uint16_t), &out_cfg, &s_jpeg_out_size);
    assert(s_jpeg_out);

    jpeg_decode_engine_cfg_t eng_cfg = { .timeout_ms = 1000 };
    ESP_ERROR_CHECK(jpeg_new_decoder_engine(&eng_cfg, &s_jpd));

    ESP_LOGI(TAG, "decoder ready, input scratch buffer %u bytes", (unsigned)s_jpeg_in_size);
}

uint8_t *tile_jpeg_input_buffer(void) { return s_jpeg_in; }
size_t   tile_jpeg_input_buffer_size(void) { return s_jpeg_in_size; }

bool tile_jpeg_decode(size_t jpeg_len, uint16_t *dst)
{
    // Decodes into a dedicated scratch buffer allocated through
    // jpeg_alloc_decoder_mem(), then memcpy()s into the caller's dst -- this
    // is the original, proven-working shape (matches what tile_flash.c did
    // before this shared module existed). An earlier version of this
    // function tried decoding directly into the caller's dst to skip that
    // copy, on the theory that tile_cache.c's slot buffers use a compatible
    // SPIRAM/alignment class -- that theory was wrong in a way that doesn't
    // surface as a driver error: jpeg_decoder_process() still returns
    // ESP_OK, but the tile silently fails to actually render (confirmed on
    // real hardware -- exercise caution generalizing "compatible allocator
    // class" claims about DMA-target buffers without an on-hardware check).
    jpeg_decode_cfg_t dec_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    uint32_t out_used = 0;
    esp_err_t err = jpeg_decoder_process(s_jpd, &dec_cfg, s_jpeg_in, (uint32_t)jpeg_len,
                                          s_jpeg_out, s_jpeg_out_size, &out_used);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "jpeg decode failed: %d", err);
        return false;
    }
    memcpy(dst, s_jpeg_out, MAP_TILE_SIZE * MAP_TILE_SIZE * sizeof(uint16_t));
    return true;
}
