// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>

// Exposes the microSD card as a USB mass-storage drive over the Tab5's
// USB-A/OTG port (a separate physical PHY from the USB-C/USB-Serial-JTAG
// port used for flashing -- see the "USB mass-storage mode" plan for why
// that split makes this safe to leave running indefinitely, no mode-switch
// escape hatch needed). Disables the port's own 5V boost first (see
// board_set_usb5v_en()) so it doesn't fight the host PC's VBUS.
//
// Only built into the USB_MSC_MODE build (see main.cmake.extra/main.c) --
// this replaces the normal map-app startup for that build, it isn't run
// alongside it.
//
// Call once, after board_init(). Returns false (and logs) on any init
// failure -- no fallback, this build has nothing else to do if it fails.
bool usb_msc_start(void);
