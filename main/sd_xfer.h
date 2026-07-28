// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Receives a file over the console (COM17/USB-Serial-JTAG) and writes it to
// the mounted SD card -- a fallback to the USB mass-storage approach
// (main/usb_msc.c), used when that path proved unreliable over the
// available A-to-A cable. See tools/send_to_sd.py for the PC-side sender
// and the line protocol both sides implement.
//
// Blocks forever, servicing one transfer after another. Never returns.
// Only built into the SD_XFER_MODE build (see main.cmake.extra/main.c).
void sd_xfer_run(void);
