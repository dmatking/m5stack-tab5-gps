// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Battery gauge for the Tab5's removable NP-F550 pack (2S Li-ion, 7.4V
// nominal, 2000mAh) via the INA226 power monitor on the board's internal
// I2C bus. Confirmed present at 0x41 and responding on real hardware
// 2026-08-15 -- same chip/address/bus M5Stack's own vendor firmware
// (M5Tab5-UserDemo, C:\vendors\M5Stack-Tab5) and M5Unified's Power_Class
// use for this board.

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Adds the INA226 as a device on board_i2c_bus_handle() and probes it once
// (a single bus-voltage register read) so later battery_get_percent() calls
// don't each pay for a failed-probe's I2C timeout. Safe to call even if the
// board has no LCD/battery (board_i2c_bus_handle() is NULL) or the chip
// doesn't respond -- battery_get_percent() just returns false from then on,
// same as never having called this. Call after board_init().
void battery_init(void);

// True if the INA226 responded at battery_init() -- doesn't mean a battery
// is physically installed (the Tab5's standard SKU ships without one, see
// this header's own comment; the chip still reads *something* off the bus
// either way), just that a real percent is worth trying to read at all.
bool battery_present(void);

// Reads the INA226's bus-voltage register (total pack voltage, no
// calibration register needed for a voltage-only read) and converts to a
// percent via the same curve M5Unified's Power_Class uses for this board:
// halve pack voltage to per-cell (2S), then (cell_mV - 3300) * 100 / 850,
// clamped 0-100 (3300mV/cell = 0%, 4150mV/cell = 100%). Returns false (out
// unchanged) if the chip never responded at battery_init() or this read
// fails -- callers should leave whatever they were already showing alone,
// not fall back to a fake number.
bool battery_get_percent(int *out_percent);

#ifdef __cplusplus
}
#endif
