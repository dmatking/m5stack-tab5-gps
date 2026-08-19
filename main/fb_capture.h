// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

// On-demand screen capture *and* remote tab switching for debugging -- lets
// tools/pull_snapshot.py grab whatever's currently on an LVGL-driven screen
// (Home/Telemetry/Goto/Settings) as a PNG on the PC, and lets a "TAB <name>"
// command switch screens without anyone tapping the device, so a whole
// screenshot sweep can be scripted end-to-end. Both instead of photographing
// the panel / having someone drive it by hand.
//
// A short "SNAP GET" command over the same USB-Serial-JTAG connection
// already used for flashing/monitoring triggers a capture (reusing
// navbar_snapshot.c's lv_snapshot_take() trick, scaled up to the whole
// active screen); the captured frame itself goes to the "snapshot" flash
// partition (see partitions.csv), not back over that same connection --
// see fb_capture.c's file header for why. tools/pull_snapshot.py reads the
// partition with `esptool read_flash` (the same proven bootloader-mode path
// already used for flashing) and converts it to a PNG.
//
// Map screen: switching *to* it works ("TAB MAP" goes through the same
// ui_show_tab() a real tap does). Switching *away* from it remotely doesn't
// -- see fb_capture.c's file header for why -- and capturing it never has:
// it's native-rendered straight to the DSI hardware framebuffers with LVGL
// stopped (see ui_shell.c's ui_shell_enter_map()), so there's no LVGL scene
// tree to snapshot there. Requests made while the Map screen is up get a
// "SNAP FAIL"/"TAB FAIL" response instead of silently doing the wrong thing.
//
// Spawns its own low-priority task and returns immediately. Call once from
// app_main(), after ui_shell_start() -- normal-mode build only, not the
// USB_MSC_MODE/SD_XFER_MODE dev-tool builds (see main.c).
void fb_capture_start(void);

// Same underlying capture as "SNAP GET" (lv_snapshot_take() of whatever
// LVGL screen is currently active -- see this header's own comment on why
// not a raw hw-framebuffer read, and why that means the Map screen still
// isn't capturable this way either), but written to a file on the SD card
// instead of the flash partition -- for capturing screens while genuinely
// untethered outdoors with no USB connection to pull SNAP GET's capture
// through. Files land in "<SD_MOUNT_POINT>/screenshots/shot_NNNN.bin",
// raw RGB565, no header; tools/sd_screenshots_to_png.py decodes them the
// same way tools/pull_snapshot.py already does. Returns false (logs its
// own reason) if the Map screen is active, the SD card isn't mounted, or
// the write fails -- callers should treat that as "nothing saved", not
// retry automatically.
bool fb_capture_save_to_sd(void);
