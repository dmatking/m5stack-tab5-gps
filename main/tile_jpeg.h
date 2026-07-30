// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Shared hardware JPEG decoder + DMA-capable scratch input buffer, used by
// both main/tile_flash.c and main/tile_sd.c so neither owns a duplicate
// decoder engine/buffer set. Call once, after board_init().
void tile_jpeg_init(void);

// The shared DMA-capable scratch buffer: callers (tile_flash_read(),
// tile_sd_read()) read raw JPEG bytes directly into this (esp_partition_read()
// or fread() respectively -- no extra copy needed, this buffer already is the
// special decoder-required allocation) before calling tile_jpeg_decode().
uint8_t *tile_jpeg_input_buffer(void);
size_t   tile_jpeg_input_buffer_size(void);

// Decode tile_jpeg_input_buffer()[0..jpeg_len) directly into dst (MAP_TILE_SIZE
// x MAP_TILE_SIZE, contiguous, RGB565) -- no internal scratch output buffer or
// extra memcpy. Returns false on a decode error (dst left untouched).
bool tile_jpeg_decode(size_t jpeg_len, uint16_t *dst);
