// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Starts a periodic task that pushes live main/gps.c data into the Home
// screen's setters (main/ui_home.h). Call once, after ui_init() has built
// the Home screen (main/design_ui.h) and gps_init() has started the GPS
// reader -- both must already exist by the first tick.
//
// Not wired to real data (deliberately, not an oversight): trip computer
// (distance/moving-time/avg-max-speed/elevation gain -- needs its own
// accumulator, a separate feature) and battery percent (no fuel-gauge
// hardware touched yet in this project). Both stay at ui_home_create()'s
// demo values rather than being fed something fake.
void gps_ui_bridge_start(void);
