// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

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
