// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Starts a periodic task that pushes live main/gps.c data into the Home and
// Telemetry screens' setters (main/ui_home.h, main/ui_telemetry.h). Also
// registers gps_ui_bridge_reset_trip() (below) as Home's Reset Trip button
// callback. Call once, after ui_init() has built both screens
// (main/design_ui.h) and gps_init() has started the GPS reader -- both must
// already exist by the first tick.
//
// Not wired to real data (deliberately, not an oversight):
// - Battery percent -- no fuel-gauge hardware touched yet in this project.
// - Telemetry's satellite signal bars + constellation list -- would need
//   GSV sentence parsing (per-satellite SNR/visibility) that main/gps.c
//   doesn't do yet. Stays at ui_telemetry_create()'s demo values.
void gps_ui_bridge_start(void);

// Zeroes the trip accumulator (distance/max-speed/moving-time/elevation-gain)
// that feeds both Home's and Telemetry's trip fields. Exposed mainly so
// ui_home_set_reset_trip_cb() has something real to call; not expected to
// be called directly elsewhere.
void gps_ui_bridge_reset_trip(void);
