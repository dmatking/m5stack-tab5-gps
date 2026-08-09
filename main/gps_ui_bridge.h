// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Starts a periodic task that pushes live main/gps.c data into the Home and
// Telemetry screens' setters (main/ui_home.h, main/ui_telemetry.h). Call
// once, after ui_init() has built both screens (main/design_ui.h) and
// gps_init() has started the GPS reader -- both must already exist by the
// first tick.
//
// Not wired to real data (deliberately, not an oversight):
// - Home's own trip widget -- needs avg-speed and elevation-gain on top of
//   what Telemetry's trip fields need (distance/max-speed/moving-time,
//   which ARE real, via a small accumulator in gps_ui_bridge.c). Stays at
//   ui_home_create()'s demo values.
// - Battery percent -- no fuel-gauge hardware touched yet in this project.
// - Telemetry's satellite signal bars + constellation list -- would need
//   GSV sentence parsing (per-satellite SNR/visibility) that main/gps.c
//   doesn't do yet. Stays at ui_telemetry_create()'s demo values.
void gps_ui_bridge_start(void);
