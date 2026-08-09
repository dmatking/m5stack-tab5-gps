// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Builds the 6 mockup screens (full-screen PNGs read straight off the SD
// card -- see main/mockup_viewer.c's file header for the paths/format) and
// loads the Home one. Call from an LVGL context (e.g. a menu button's
// click callback in ui_shell.c) -- it takes the LVGL port lock itself, so
// don't call it while already holding it.
void mockup_viewer_start(void);
