// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// On-demand screen capture for debugging -- lets tools/pull_snapshot.py
// grab whatever's currently on an LVGL-driven screen (Home/Telemetry/Goto/
// Settings) as a PNG on the PC, instead of photographing the panel.
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
// Map screen NOT supported yet -- it's native-rendered straight to the DSI
// hardware framebuffers with LVGL stopped (see ui_shell.c's
// ui_shell_enter_map()), so there's no LVGL scene tree to snapshot there.
// Requests made while the Map screen is up get a "SNAP FAIL" response
// instead of silently capturing stale content from whatever screen was
// loaded before the handoff.
//
// Spawns its own low-priority task and returns immediately. Call once from
// app_main(), after ui_shell_start() -- normal-mode build only, not the
// USB_MSC_MODE/SD_XFER_MODE dev-tool builds (see main.c).
void fb_capture_start(void);
