// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Captures a real, LVGL-rendered copy of main/ui_common.c's navbar (with
// the Map tab active -- the only state the Map screen ever needs, since
// it's always the active tab while that screen is up) as a plain RGB565
// pixel buffer, so the native (non-LVGL) Map screen can just blit real
// pixels instead of re-implementing font/icon rendering itself.
//
// This replaced an earlier attempt that called LVGL's own glyph-bitmap API
// (lv_font_get_glyph_dsc()/lv_font_get_glyph_bitmap()) directly from the
// native render path to draw the real fonts/icons live, every frame. That
// shipped once, crashed real hardware (root cause not fully pinned down --
// reverted before it was), and was strictly more code and more novel-risk
// than this for a navbar that never actually changes: only ONE frame of
// it is ever needed, captured ONCE, through LVGL's own normal render
// pipeline while LVGL is still fully running (before the Map screen ever
// stops it) -- not live, not through internals nothing else in this
// project reaches into.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Builds a throwaway, never-shown navbar (main/ui_common.c's
// ui_navbar_create(), UI_TAB_MAP active), renders it via LVGL's
// lv_snapshot_take(), and copies the result into a PSRAM buffer this
// module owns. Call once, after design_ui.h's ui_init() has run (needs
// ui_theme_init() and a live default display already set up) and while
// still holding the LVGL port lock -- same call-site contract as ui_init()
// itself. Safe to call again later (e.g. if the real navbar's palette
// ever changes) -- re-captures and replaces the stored buffer.
void navbar_snapshot_capture(void);

// Returns the captured buffer (UI_SCREEN_W x UI_NAVBAR_H pixels -- see
// main/ui_theme.h -- row-major, no padding, tightly packed RGB565), or
// NULL if navbar_snapshot_capture() hasn't run yet or failed. Safe to call
// from the native (non-LVGL) render path with no locking -- the buffer is
// written once and never mutated afterward.
const uint16_t *navbar_snapshot_get(void);

#ifdef __cplusplus
}
#endif
