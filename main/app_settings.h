// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Small persisted (NVS) settings store. This project has never used NVS
// before -- the "nvs" partition in partitions.csv existed but sat unused --
// so this is the first real use of it, deliberately kept minimal (one
// bool) rather than building out a general preferences framework ahead of
// having more than one real setting to put in it. Add fields/get/set pairs
// here as more Settings-screen rows go from decorative to real.

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opens (creating if needed) the "app_settings" NVS namespace and loads
// every setting into memory. Call once, early in app_main() -- before
// ui_shell_start(), which builds the Settings screen and needs the real
// persisted values to show at creation time. Safe to call even if NVS
// needs a first-time erase (handles ESP_ERR_NVS_NO_FREE_PAGES/
// NEW_VERSION_FOUND the standard ESP-IDF way) or the namespace doesn't
// exist yet (first boot ever -- falls back to defaults, doesn't fail).
void app_settings_init(void);

// false = 12-hour ("3:24 PM"), true = 24-hour ("15:24"). Default (first
// boot, or NVS read failure) is false, matching the original design's own
// 12-hour demo strings ("10:24 AM").
bool app_settings_get_time_24h(void);

// Updates the in-memory value AND commits it to NVS immediately -- a
// physical GPS handheld can lose power at any time (battery pull, not a
// clean shutdown), so there's no "save on exit" moment to rely on instead.
void app_settings_set_time_24h(bool on);

// Backlight brightness, 5-100 (matches ui_settings.c's slider range).
// Default 72, matching the original design's own demo value.
int app_settings_get_brightness(void);
void app_settings_set_brightness(int percent);

// Whether the idle screen-off timeout (ui_shell.c/map_view.c, both share
// this) is allowed to fire at all. Default true (screen stays on) --
// matches ui_settings_create()'s own creation-time switch state.
bool app_settings_get_keep_screen_on(void);
void app_settings_set_keep_screen_on(bool on);

// Coordinate display/entry format: 0=DD MM.MMMM, 1=DD.DDDDDD, 2=DD MM SS --
// matches ui_goto.h's ui_coord_fmt_t numeric values exactly (deliberately
// not typed as that enum here, to keep this lower-level file independent
// of UI-layer headers; callers cast). Feeds Home's position card display
// and seeds Goto's format toggle at boot -- Goto's own toggle stays freely
// overridable per-entry from there, this only sets where it starts.
int app_settings_get_coord_format(void);
void app_settings_set_coord_format(int fmt);

// Distance/speed unit: false = mi/mph (default), true = km/h. Applies to
// Home's SPEED/TRIP cards and the Nav screen's distance/speed/closure.
bool app_settings_get_distance_km(void);
void app_settings_set_distance_km(bool km);

// Elevation unit: false = feet (default), true = meters. Applies to Home's
// ALTITUDE/GPS ACCURACY/TRIP elevation-gain and Telemetry's VERTICAL SPEED
// (feet-per-minute becomes meters-per-minute -- same axis, a rate of
// elevation change, not a second independent unit choice).
bool app_settings_get_elevation_m(void);
void app_settings_set_elevation_m(bool meters);

#ifdef __cplusplus
}
#endif
