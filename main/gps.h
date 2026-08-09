// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Adapted from esp32-idf-new/modules/gps/_common/gps.h -- the reusable
// NMEA parser this project's earlier ad-hoc raw-line listener has been
// replaced with. Pins/baud are hardcoded for the Tab5's M5-Bus UART0
// pins (this project's convention) instead of that module's Kconfig
// options -- see [[reference-gps-module-hw]] for how those values (UART1,
// RX=GPIO38, TX=GPIO37, DIP switch 6, 115200 8N1) were confirmed on real
// hardware with the M5Stack GPS Module v2.1.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Parsed GPS state. Updated by the background reader task; read via gps_get_state().
typedef struct {
    bool gga_fix;           // GGA sentence reports a fix
    bool rmc_fix;           // RMC sentence reports active status (A or D)
    bool latlon_valid;      // latitude_deg / longitude_deg are valid
    bool time_valid;        // utc_tm time fields are valid
    bool date_valid;        // utc_tm date fields are valid
    bool speed_valid;       // speed_knots is valid
    bool hdop_valid;        // hdop is valid
    bool altitude_valid;    // altitude_m is valid
    double latitude_deg;    // decimal degrees, negative = south
    double longitude_deg;   // decimal degrees, negative = west
    float speed_knots;
    float heading_deg;      // true track from RMC
    float hdop;             // horizontal dilution of precision (lower = better)
    float altitude_m;       // antenna altitude above mean sea level, meters
    int sats_in_use;
    struct tm utc_tm;       // UTC time/date (populated incrementally from GGA + RMC)
} gps_state_t;

// Initialize UART1 (Tab5 M5-Bus RXD0/TXD0, GPIO38/37) and start the
// background NMEA reader task. Safe to call even if no module is attached.
void gps_init(void);

// Thread-safe snapshot of the current GPS state.
gps_state_t gps_get_state(void);

// True if either GGA or RMC reports a fix.
bool gps_has_fix(void);

// True if the SD card is mounted, the log file is open, and the most
// recent write to it (fprintf+fflush+fsync) actually succeeded -- not just
// that fopen() succeeded at boot. A card that mounts fine but is full,
// write-protected, or gets pulled mid-session should show up here.
bool gps_log_active(void);

#ifdef __cplusplus
}
#endif
