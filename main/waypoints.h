// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Saved waypoints, persisted in their own NVS partition (`waypoints`, see
// partitions.csv). Deliberately minimal, matching how this feature is
// actually used on the device: you mark where you're standing, you pick one
// later to navigate to, you delete ones you're done with. There is no
// rename, no folders, no categories, no import/export.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAYPOINTS_MAX     500
// "WPT 001" + NUL with room to spare. Fixed rather than a pointer so the
// whole store is one flat, memcpy-able array -- and there's no rename UI,
// so this is permanently sufficient.
#define WAYPOINT_NAME_LEN 12

// 32 bytes exactly, no padding (double is 8-aligned and leads, so the
// struct's alignment requirement is satisfied without holes). That's not
// cosmetic: it keeps the whole store at 500 * 32 = 16000 bytes, which fits
// the 64KB partition with room for NVS's own page headers and its reserved
// compaction page, and keeps the in-RAM cache to 16KB.
typedef struct {
    double   lat;
    double   lon;
    uint32_t seq;                       // the N in "WPT NNN"; unique among stored
    char     name[WAYPOINT_NAME_LEN];   // NUL-terminated
} waypoint_t;

typedef enum {
    WAYPOINTS_OK = 0,
    WAYPOINTS_ERR_FULL,         // WAYPOINTS_MAX already stored
    WAYPOINTS_ERR_STORE,        // NVS write/commit failed -- nothing was kept
    WAYPOINTS_ERR_UNAVAILABLE,  // partition missing, or init never succeeded
} waypoints_err_t;

// Mounts the `waypoints` NVS partition and loads the store into RAM. Call
// once, after app_settings_init(). Never fatal: if the partition is missing
// or unmountable this logs and disables the store, and every call below
// then returns WAYPOINTS_ERR_UNAVAILABLE / false rather than crashing.
// Unlike app_settings_init() this deliberately does NOT ESP_ERROR_CHECK --
// settings are load-bearing for the whole UI, saved waypoints are not, and
// panic-looping the boot over them would be the wrong trade.
void waypoints_init(void);

int  waypoints_count(void);

// index 0 is the most recently added -- the store is kept newest-first so
// the list the user sees is just the array order, no sorting step.
bool waypoints_get(int index, waypoint_t *out);

// Saves a position under an auto-generated "WPT NNN". out_created may be
// NULL; when non-NULL it receives the stored record (its name is what you
// want to show as confirmation). On WAYPOINTS_ERR_STORE the in-RAM list is
// rolled back, so RAM and flash never disagree -- see waypoints.c.
waypoints_err_t waypoints_add(double lat, double lon, waypoint_t *out_created);

waypoints_err_t waypoints_delete(int index);

#ifdef __cplusplus
}
#endif
