// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Persists the Trip card's running totals (main/gps_ui_bridge.c's own
// s_trip_miles/s_max_mph/s_moving_ticks/s_elev_gain_ft) across a reboot --
// previously session-only, which meant any power cycle (planned or a
// battery just dying outdoors) silently zeroed a trip in progress with no
// way to get it back. Deliberately NOT its own NVS partition like
// main/waypoints.h -- this is four scalars, not a 500-record store, so it
// shares the same default "nvs" partition main/app_settings.c already
// uses, under its own namespace.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    distance_mi;
    float    max_mph;
    uint32_t moving_s;      // seconds, not ticks -- independent of TICK_PERIOD_MS
    float    elev_gain_ft;
} trip_totals_t;

// Loads whatever was last saved into *out -- all-zero if nothing's been
// stored yet, the store is unavailable, or the stored blob doesn't match
// this struct's current size (a foreign/stale blob is dropped rather than
// half-trusted). Call once at boot, before the first tick that would
// otherwise start every total at zero.
void trip_store_load(trip_totals_t *out);

// Overwrites the persisted totals. Not throttled internally -- writing to
// NVS on every 2Hz tick would be real write amplification over a
// multi-hour drive (7000+ writes/hour) for no benefit, so
// gps_ui_bridge.c's tick() decides how often this is worth calling (see
// its own comment) and only calls it when something actually changed.
void trip_store_save(const trip_totals_t *t);

// Equivalent to trip_store_save() with every field zero -- Reset Trip's
// stored totals should disappear as immediately as its in-RAM ones do,
// not wait for the next periodic save.
void trip_store_clear(void);

#ifdef __cplusplus
}
#endif
