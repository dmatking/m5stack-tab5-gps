// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Reads and decodes JPEG map tiles out of sharded blob files on the SD card
// -- the much-larger-capacity counterpart to main/tile_flash.c's small
// compile-time-embedded flash region, for real geographic coverage that
// won't fit in 14MB of flash. Generated offline by tools/fetch_tiles.py
// (--target sd).
//
// Sharded rather than one big blob: a single multi-GB file is broken on
// this project's actual toolchain/filesystem config in three independent
// ways -- fseek()/off_t is 32-bit here (~2GB ceiling), FAT32 itself caps a
// single file at 4GB-1, and CONFIG_FATFS_USE_FASTSEEK is off in this
// project's sdkconfig, so FatFs's f_lseek() walks the FAT chain from the
// file's first cluster on any backward/far-forward seek -- and panning is
// inherently non-monotonic access (verified against the actual toolchain/
// ESP-IDF FatFs source, not assumed). Each shard covers a
// bounded sub-rectangle of one zoom level's grid (tools/fetch_tiles.py's
// --sd-shard-max-tiles), keeping shard files well under both hard caps.
//
// Shard discovery is by filename convention, not a separate manifest file
// (self-synchronizing -- can't go stale if shards are added/removed/only
// partially copied onto the card):
//   tiles_sd_z<zoom>_x<base_tx>_y<base_ty>_c<cols>_r<rows>.bin
// Every grid parameter is already in the filename, so shard files carry no
// header of their own -- just the index table followed by JPEG data:
//   [cols*rows x {uint32_t offset, uint32_t length}]  -- offsets absolute
//                                                         from shard start
//   [JPEG data, packed back-to-back]
//
// At most one shard file is kept open at a time (closed/reopened on a
// cross-shard tile request) -- SD_MAX_FILES=4 is shared with gps.c's
// persistent log handle, and typical panning stays within one shard anyway.
// Decoding is shared with main/tile_flash.c via main/tile_jpeg.c.

#include "tile_sd.h"
#include "sd_card.h"
#include "tile_jpeg.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"

static const char *TAG = "TILE_SD";

typedef struct {
    int32_t zoom, base_tx, base_ty, cols, rows;
    char filename[256]; // dirent's d_name can theoretically be up to NAME_MAX
} sd_shard_meta_t;

typedef struct { uint32_t offset, length; } tile_index_entry_t;

static sd_shard_meta_t *s_shards = NULL;
static int s_shard_count = 0;

// The one currently-open shard's file handle + preloaded index table (see
// file header -- kept small deliberately, at most one open at a time).
static int s_open_shard_idx = -1;
static FILE *s_open_file = NULL;
static tile_index_entry_t *s_open_index = NULL;

void tile_sd_init(void)
{
    if (!sd_card_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted -- SD tiles disabled");
        return;
    }

    DIR *dir = opendir(SD_MOUNT_POINT);
    if (!dir) {
        ESP_LOGW(TAG, "opendir(%s) failed -- SD tiles disabled", SD_MOUNT_POINT);
        return;
    }

    int capacity = 8;
    s_shards = malloc((size_t)capacity * sizeof(sd_shard_meta_t));
    if (!s_shards) {
        ESP_LOGE(TAG, "out of memory scanning for SD tile shards");
        closedir(dir);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        // %d expects plain `int`, not int32_t -- on this toolchain int32_t is
        // `long`, a distinct type from `int` despite matching width, so
        // sscanf needs plain-int locals here rather than int32_t directly.
        int zoom, base_tx, base_ty, cols, rows;
        if (sscanf(ent->d_name, "tiles_sd_z%d_x%d_y%d_c%d_r%d.bin",
                   &zoom, &base_tx, &base_ty, &cols, &rows) != 5) {
            continue; // not a shard file -- tiles.bin, gps_log.txt, etc.
        }
        if (s_shard_count == capacity) {
            capacity *= 2;
            sd_shard_meta_t *grown = realloc(s_shards, (size_t)capacity * sizeof(sd_shard_meta_t));
            if (!grown) {
                ESP_LOGE(TAG, "out of memory growing SD tile shard list");
                break;
            }
            s_shards = grown;
        }
        sd_shard_meta_t *m = &s_shards[s_shard_count++];
        m->zoom = zoom; m->base_tx = base_tx; m->base_ty = base_ty; m->cols = cols; m->rows = rows;
        snprintf(m->filename, sizeof(m->filename), "%s", ent->d_name);
    }
    closedir(dir);

    ESP_LOGI(TAG, "found %d SD tile shard(s)", s_shard_count);
    for (int i = 0; i < s_shard_count; i++) {
        sd_shard_meta_t *m = &s_shards[i];
        ESP_LOGI(TAG, "  zoom %ld: grid %ldx%ld @ base (%ld,%ld) -- %s",
                 (long)m->zoom, (long)m->cols, (long)m->rows, (long)m->base_tx, (long)m->base_ty, m->filename);
    }
}

static int find_shard(int32_t tx, int32_t ty, int32_t zoom)
{
    for (int i = 0; i < s_shard_count; i++) {
        sd_shard_meta_t *m = &s_shards[i];
        if (m->zoom != zoom) continue;
        int32_t col = tx - m->base_tx;
        int32_t row = ty - m->base_ty;
        if (col >= 0 && col < m->cols && row >= 0 && row < m->rows) return i;
    }
    return -1;
}

// Closes/reopens the currently-cached shard if a different one is needed,
// reading the newly-opened shard's full index table into RAM (bounded --
// even tens of thousands of tiles is only a few hundred KB, see file header).
static bool ensure_shard_open(int idx)
{
    if (idx == s_open_shard_idx) return true;

    if (s_open_file) { fclose(s_open_file); s_open_file = NULL; }
    free(s_open_index); s_open_index = NULL;
    s_open_shard_idx = -1;

    sd_shard_meta_t *m = &s_shards[idx];
    char path[300];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, m->filename);

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "fopen(%s) failed", path);
        return false;
    }

    uint32_t tile_count = (uint32_t)(m->cols * m->rows);
    tile_index_entry_t *index = malloc((size_t)tile_count * sizeof(tile_index_entry_t));
    if (!index) {
        ESP_LOGE(TAG, "out of memory for shard index (%lu tiles, %s)", (unsigned long)tile_count, m->filename);
        fclose(f);
        return false;
    }
    if (fread(index, sizeof(tile_index_entry_t), tile_count, f) != tile_count) {
        ESP_LOGW(TAG, "short read on index table for %s", m->filename);
        free(index);
        fclose(f);
        return false;
    }

    s_open_file = f;
    s_open_index = index;
    s_open_shard_idx = idx;
    return true;
}

bool tile_sd_read(int32_t tile_x, int32_t tile_y, int32_t zoom, uint16_t *dst)
{
    if (s_shard_count == 0) return false;

    int idx = find_shard(tile_x, tile_y, zoom);
    if (idx < 0) return false; // no shard covers this tile

    if (!ensure_shard_open(idx)) return false;

    sd_shard_meta_t *m = &s_shards[idx];
    int32_t col = tile_x - m->base_tx;
    int32_t row = tile_y - m->base_ty;
    uint32_t local_idx = (uint32_t)(row * m->cols + col);
    tile_index_entry_t entry = s_open_index[local_idx];

    if (entry.length == 0 || entry.length > tile_jpeg_input_buffer_size()) {
        ESP_LOGW(TAG, "bad index entry for tile %ld,%ld z%ld in %s: length=%u",
                 (long)tile_x, (long)tile_y, (long)zoom, m->filename, (unsigned)entry.length);
        return false;
    }

    if (fseek(s_open_file, (long)entry.offset, SEEK_SET) != 0) {
        ESP_LOGW(TAG, "seek failed for tile %ld,%ld z%ld", (long)tile_x, (long)tile_y, (long)zoom);
        return false;
    }
    if (fread(tile_jpeg_input_buffer(), 1, entry.length, s_open_file) != entry.length) {
        ESP_LOGW(TAG, "short read for tile %ld,%ld z%ld", (long)tile_x, (long)tile_y, (long)zoom);
        return false;
    }

    // TEMPORARY verification log -- remove once confirmed on hardware. Fires
    // after decode, not before, so it actually reflects success/failure
    // (an earlier version logged "served" before decoding and was
    // misleading -- the read can succeed while decode still fails).
    bool ok = tile_jpeg_decode(entry.length, dst);
    ESP_LOGI(TAG, "tile %ld,%ld z%ld from %s: offset=%u length=%u decode=%s",
             (long)tile_x, (long)tile_y, (long)zoom, m->filename,
             (unsigned)entry.offset, (unsigned)entry.length, ok ? "OK" : "FAILED");
    return ok;
}
