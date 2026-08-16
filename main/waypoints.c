// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "waypoints.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "WAYPOINTS";

// The on-flash blob is a raw array of these, and store_load() recovers the
// count by dividing the blob length by this size -- so the layout is part
// of the persisted format, not just an implementation detail. If it ever
// changes, KEY_VER exists to migrate on; this assert makes sure that's a
// deliberate decision rather than a silent one.
_Static_assert(sizeof(waypoint_t) == 32, "waypoint_t is the persisted record layout");

#define WPT_PARTITION  "waypoints"
#define WPT_NAMESPACE  "wpts"
#define KEY_LIST       "list"   // blob: count * sizeof(waypoint_t), newest first
#define KEY_VER        "ver"    // u8, schema version for any future migration
#define WPT_VER        1

// The whole store, mirrored in RAM. 16KB of .bss at full capacity -- worth
// it to keep every read a plain array access rather than an NVS round trip
// (the Goto list rebuilds on every show, and the bridge may consult it).
static waypoint_t s_list[WAYPOINTS_MAX];
static int        s_count;
static bool       s_available;

// Writes the whole list back as one blob. Deliberately not one NVS key per
// waypoint: each key costs a blob-index entry plus a data-entry header on
// top of the payload (~128 bytes for a 32-byte record), so 500 of them
// wouldn't fit the 64KB partition once page headers and NVS's reserved
// compaction page are subtracted. One variable-length blob is 16KB at full
// capacity and a few hundred bytes in realistic use.
//
// Write amplification is a non-issue at this scale: 500 lifetime saves
// averaging ~8KB each is ~4MB through a 64KB partition, roughly 60 erase
// cycles against a ~100k endurance.
static waypoints_err_t store_commit(void)
{
    if (!s_available) return WAYPOINTS_ERR_UNAVAILABLE;

    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(WPT_PARTITION, WPT_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return WAYPOINTS_ERR_STORE;
    }

    err = nvs_set_blob(h, KEY_LIST, s_list, (size_t)s_count * sizeof(waypoint_t));
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_VER, WPT_VER);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "commit failed: %s", esp_err_to_name(err));
        return WAYPOINTS_ERR_STORE;
    }
    return WAYPOINTS_OK;
}

static void store_load(void)
{
    nvs_handle_t h;
    if (nvs_open_from_partition(WPT_PARTITION, WPT_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no store yet -- starting empty");   // first boot, not an error
        return;
    }

    size_t len = 0;
    esp_err_t err = nvs_get_blob(h, KEY_LIST, NULL, &len);
    if (err == ESP_OK && len > 0) {
        // Reject anything that isn't a whole number of records, or that
        // claims more than capacity -- a truncated or foreign blob is
        // better dropped than half-parsed into plausible-looking garbage
        // coordinates.
        if (len % sizeof(waypoint_t) != 0 || len > sizeof(s_list)) {
            ESP_LOGW(TAG, "stored blob is %u bytes (not a whole number of %u-byte records, "
                          "or over capacity) -- discarding", (unsigned)len,
                     (unsigned)sizeof(waypoint_t));
        } else if (nvs_get_blob(h, KEY_LIST, s_list, &len) == ESP_OK) {
            s_count = (int)(len / sizeof(waypoint_t));
        }
    }
    nvs_close(h);
}

void waypoints_init(void)
{
    esp_err_t err = nvs_flash_init_partition(WPT_PARTITION);
    if (err == ESP_ERR_NOT_FOUND) {
        // Running an app built against a newer partition table than what's
        // actually flashed. Degrade rather than crash.
        ESP_LOGE(TAG, "no '%s' partition -- saved waypoints disabled this boot", WPT_PARTITION);
        return;
    }
    if (err != ESP_OK) {
        // Expected exactly once, on the first boot after this partition was
        // added: 0x620000 is inside what used to be the 14MB `tiledata`
        // partition and still holds real tile bytes, so NVS finds garbage
        // rather than erased flash. See partitions.csv's own note.
        ESP_LOGW(TAG, "init failed (%s) -- formatting %s", esp_err_to_name(err), WPT_PARTITION);
        nvs_flash_erase_partition(WPT_PARTITION);
        err = nvs_flash_init_partition(WPT_PARTITION);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not mount %s (%s) -- saved waypoints disabled this boot",
                 WPT_PARTITION, esp_err_to_name(err));
        return;
    }

    s_available = true;
    store_load();
    ESP_LOGI(TAG, "%d waypoint%s stored", s_count, s_count == 1 ? "" : "s");
}

int waypoints_count(void)
{
    return s_count;
}

bool waypoints_get(int index, waypoint_t *out)
{
    if (!out || index < 0 || index >= s_count) return false;
    *out = s_list[index];
    return true;
}

// Lowest unused number in 1..999, rather than max(seq)+1: with a 500-entry
// cap there is always one free, and max+1 has no defined behaviour once it
// passes 999. Deleting a waypoint therefore frees its number for reuse --
// accepted deliberately, since the alternative is a monotonic counter that
// eventually runs out of digits anyway and there's no rename UI to fix a
// name you don't like.
static uint32_t next_free_seq(void)
{
    for (uint32_t candidate = 1; candidate <= 999; candidate++) {
        bool taken = false;
        for (int i = 0; i < s_count; i++) {
            if (s_list[i].seq == candidate) { taken = true; break; }
        }
        if (!taken) return candidate;
    }
    return 999;   // unreachable while WAYPOINTS_MAX < 999
}

waypoints_err_t waypoints_add(double lat, double lon, waypoint_t *out_created)
{
    if (!s_available) return WAYPOINTS_ERR_UNAVAILABLE;
    if (s_count >= WAYPOINTS_MAX) return WAYPOINTS_ERR_FULL;

    // Newest first: shift everything down one and take slot 0.
    memmove(&s_list[1], &s_list[0], (size_t)s_count * sizeof(waypoint_t));
    s_count++;

    waypoint_t *w = &s_list[0];
    memset(w, 0, sizeof(*w));
    w->lat = lat;
    w->lon = lon;
    w->seq = next_free_seq();
    snprintf(w->name, sizeof(w->name), "WPT %03u", (unsigned)w->seq);

    waypoints_err_t rc = store_commit();
    if (rc != WAYPOINTS_OK) {
        // Roll the insert back out of RAM. A setting that silently reverts
        // at the next boot is a nuisance; a waypoint that appears in the
        // list and then vanishes is a navigational lie, so RAM and flash
        // are never allowed to disagree here. (This is the one place this
        // module deliberately diverges from app_settings.c's "applied this
        // session only" behaviour on a failed write.)
        memmove(&s_list[0], &s_list[1], (size_t)(s_count - 1) * sizeof(waypoint_t));
        s_count--;
        return rc;
    }

    if (out_created) *out_created = *w;
    ESP_LOGI(TAG, "saved %s at %.6f, %.6f (%d stored)", w->name, lat, lon, s_count);
    return WAYPOINTS_OK;
}

waypoints_err_t waypoints_delete(int index)
{
    if (!s_available) return WAYPOINTS_ERR_UNAVAILABLE;
    if (index < 0 || index >= s_count) return WAYPOINTS_ERR_STORE;

    waypoint_t removed = s_list[index];
    memmove(&s_list[index], &s_list[index + 1],
            (size_t)(s_count - index - 1) * sizeof(waypoint_t));
    s_count--;

    waypoints_err_t rc = store_commit();
    if (rc != WAYPOINTS_OK) {
        // Same rollback contract as add(): put it back so the list on
        // screen keeps matching what's actually in flash.
        memmove(&s_list[index + 1], &s_list[index],
                (size_t)(s_count - index) * sizeof(waypoint_t));
        s_list[index] = removed;
        s_count++;
        return rc;
    }

    ESP_LOGI(TAG, "deleted %s (%d stored)", removed.name, s_count);
    return WAYPOINTS_OK;
}
