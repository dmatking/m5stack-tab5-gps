// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "tile_synth.h"
#include "map_config.h"

#include <stdbool.h>
#include <stddef.h>

// Every zoom change lands 100% in unexplored territory (the cache is keyed
// by zoom, so a fresh zoom level always starts as an all-miss placeholder
// screen), unlike panning where only a thin edge is ever new -- so any
// green-leaning entry here reads as a jarring "everything just turned
// green" flash on every single zoom step. Deliberately no channel is
// green(G)-dominant in any of these.
static const uint16_t PALETTE[] = {
    0xACB1, // warm gray
    0x94F5, // cool gray
    0xCDB2, // tan
    0x8CB6, // dusty blue
    0xBCF4, // dusty rose
    0x7C11, // slate
    0xD614, // sand
    0xA4D6, // lavender gray
};
#define PALETTE_LEN (sizeof(PALETTE) / sizeof(PALETTE[0]))

static uint32_t hash2(int32_t x, int32_t y, int32_t z)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline uint16_t shade(uint16_t c, int delta_r, int delta_g, int delta_b)
{
    int r = (c >> 11) & 0x1F;
    int g = (c >> 5) & 0x3F;
    int b = c & 0x1F;
    r += delta_r; g += delta_g; b += delta_b;
    if (r < 0) r = 0; else if (r > 31) r = 31;
    if (g < 0) g = 0; else if (g > 63) g = 63;
    if (b < 0) b = 0; else if (b > 31) b = 31;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

void synth_tile(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst)
{
    uint32_t h = hash2(tile_x, tile_y, zoom);
    uint16_t base = PALETTE[h % PALETTE_LEN];
    uint16_t alt  = shade(base, -3, -6, -3);
    uint16_t border = shade(base, 10, 12, 10);

    const int cell = 32;

    for (int y = 0; y < MAP_TILE_SIZE; y++) {
        bool on_h_border = (y < 2 || y >= MAP_TILE_SIZE - 2);
        uint16_t *row = dst + (size_t)y * MAP_TILE_SIZE;
        for (int x = 0; x < MAP_TILE_SIZE; x++) {
            bool on_border = on_h_border || x < 2 || x >= MAP_TILE_SIZE - 2;
            if (on_border) {
                row[x] = border;
            } else {
                bool checker = ((x / cell) + (y / cell)) % 2 == 0;
                row[x] = checker ? base : alt;
            }
        }
    }
}
