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
void gps_ui_bridge_start(void);

// Zeroes the trip accumulator (distance/max-speed/moving-time/elevation-gain)
// that feeds both Home's and Telemetry's trip fields. Exposed mainly so
// ui_home_set_reset_trip_cb() has something real to call; not expected to
// be called directly elsewhere.
void gps_ui_bridge_reset_trip(void);

// Saves the current GPS position as a waypoint (main/waypoints.h) and
// flashes the outcome on Home's Mark Position button. Lives here rather
// than in design_ui.c because it needs the live fix. Wired as Home's mark
// callback by gps_ui_bridge_start(); no-ops with a message if there's no
// fix to save.
void gps_ui_bridge_mark_waypoint(void);
