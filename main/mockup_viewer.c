// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Displays 6 full-screen UI mockup PNGs (visual design references, not a
// real feature yet) with tap navigation between them. Pure LVGL -- these
// are just static pictures, not the performance-critical map path, so
// there's no reason to reinvent the native PPA blit route here.
//
// The images live on the SD card (see tools/send_to_sd.py / main/sd_xfer.c
// for how they got there -- "big media on the SD card, not embedded in
// flash" is the same pattern the map tiles use), read through LVGL's
// stdio filesystem driver (LV_USE_FS_STDIO, drive letter 'S') and decoded
// with its bundled LodePNG decoder (LV_USE_LODEPNG) -- see
// sdkconfig.defaults. All 6 PNGs are exactly 720x1280, matching this
// panel's native resolution exactly, so each is shown at (0,0) with no
// scaling.
//
// Each screen gets a set of invisible "hotspot" buttons: the bottom tab
// bar (Home/Map/Nav/Telemetry/Settings, identical position on every
// mockup -- measured by sampling pixel colors across the divider line, see
// TABBAR_Y) plus a few screen-specific ones for flows the mockups imply
// but don't label explicitly (Nav's destination header -> Goto, Nav's
// "Map View" button -> Map, Goto's Cancel/Start Navigation -> back to
// Nav). Those last few are guesses at intent, not specified anywhere --
// easy to move/remove once seen on hardware.

#include "mockup_viewer.h"

#include "map_config.h"
#include "ui_shell.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "MOCKUP";

typedef enum {
    MOCKUP_HOME,
    MOCKUP_MAP,
    MOCKUP_NAV,
    MOCKUP_TELEMETRY,
    MOCKUP_GOTO,
    MOCKUP_SETTINGS,
    MOCKUP_COUNT,
} mockup_id_t;

static const char *s_paths[MOCKUP_COUNT] = {
    "S:/sdcard/mockup_home.png",
    "S:/sdcard/mockup_map.png",
    "S:/sdcard/mockup_nav.png",
    "S:/sdcard/mockup_telemetry.png",
    "S:/sdcard/mockup_goto.png",
    "S:/sdcard/mockup_settings.png",
};

static lv_obj_t *s_screens[MOCKUP_COUNT];

// Tab bar geometry, identical on all 6 mockups -- measured by sampling
// pixel colors down a vertical strip in mockups_extract/export/2a-01-home.png:
// background shifts from (5,8,12) to the tab-bar panel's (8,12,17) right
// around y=1203, with icon/label pixels starting a few rows below that.
#define TABBAR_Y      1195
#define TABBAR_H      (MAP_LOGICAL_H - TABBAR_Y)
#define TABBAR_COL_W  (MAP_LOGICAL_W / 5)

static void goto_screen_cb(lv_event_t *e)
{
    mockup_id_t id = (mockup_id_t)(intptr_t)lv_event_get_user_data(e);
    lv_screen_load(s_screens[id]);
}

// The mockups don't show any obvious "leave the app" affordance (they're
// meant to BE the whole app) -- a small unlabeled hotspot in the unused
// top-left corner (below the status bar's fix dot, above the tab bar)
// gets back to ui_shell's real main menu.
static void back_to_menu_cb(lv_event_t *e)
{
    (void)e;
    ui_shell_show_main_menu();
}

static lv_obj_t *make_hotspot(lv_obj_t *parent, int x, int y, int w, int h, mockup_id_t target)
{
    lv_obj_t *hot = lv_obj_create(parent);
    lv_obj_set_pos(hot, x, y);
    lv_obj_set_size(hot, w, h);
    lv_obj_set_style_bg_opa(hot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hot, 0, 0);
    lv_obj_set_style_radius(hot, 0, 0);
    lv_obj_add_event_cb(hot, goto_screen_cb, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    return hot;
}

static void add_tab_bar(lv_obj_t *screen)
{
    make_hotspot(screen, 0 * TABBAR_COL_W, TABBAR_Y, TABBAR_COL_W, TABBAR_H, MOCKUP_HOME);
    make_hotspot(screen, 1 * TABBAR_COL_W, TABBAR_Y, TABBAR_COL_W, TABBAR_H, MOCKUP_MAP);
    make_hotspot(screen, 2 * TABBAR_COL_W, TABBAR_Y, TABBAR_COL_W, TABBAR_H, MOCKUP_NAV);
    make_hotspot(screen, 3 * TABBAR_COL_W, TABBAR_Y, TABBAR_COL_W, TABBAR_H, MOCKUP_TELEMETRY);
    make_hotspot(screen, 4 * TABBAR_COL_W, TABBAR_Y, TABBAR_COL_W, TABBAR_H, MOCKUP_SETTINGS);
}

static lv_obj_t *build_screen(const char *path)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *img = lv_image_create(screen);
    lv_image_set_src(img, path);
    lv_obj_set_pos(img, 0, 0);

    lv_obj_t *back = lv_obj_create(screen);
    lv_obj_set_pos(back, 0, 0);
    lv_obj_set_size(back, 100, 55);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_add_event_cb(back, back_to_menu_cb, LV_EVENT_CLICKED, NULL);

    return screen;
}

void mockup_viewer_start(void)
{
    if (!lvgl_port_lock(0)) {
        ESP_LOGW(TAG, "couldn't take the LVGL lock");
        return;
    }

    for (int i = 0; i < MOCKUP_COUNT; i++) {
        s_screens[i] = build_screen(s_paths[i]);
    }
    for (int i = 0; i < MOCKUP_COUNT; i++) {
        add_tab_bar(s_screens[i]);
    }

    // Screen-specific extras, on top of the shared tab bar above -- see
    // this file's header comment. Coordinates are eyeballed from the
    // mockup images, not pixel-measured like the tab bar.
    make_hotspot(s_screens[MOCKUP_NAV], 20, 70, 680, 110, MOCKUP_GOTO);    // destination header -> Goto
    make_hotspot(s_screens[MOCKUP_NAV], 20, 1050, 330, 90, MOCKUP_MAP);    // "Map View" button -> Map
    make_hotspot(s_screens[MOCKUP_GOTO], 20, 1060, 220, 90, MOCKUP_NAV);   // "Cancel" -> Nav
    make_hotspot(s_screens[MOCKUP_GOTO], 255, 1060, 445, 90, MOCKUP_NAV);  // "Start Navigation" -> Nav

    lv_screen_load(s_screens[MOCKUP_HOME]);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "mockup viewer ready");
}
