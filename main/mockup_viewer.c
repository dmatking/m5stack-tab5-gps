// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Displays 6 full-screen UI mockup images (visual design references, not a
// real feature yet) with tap navigation between them. Pure LVGL -- these
// are just static pictures, not the performance-critical map path, so
// there's no reason to reinvent the native PPA blit route here.
//
// Images are raw RGB565, not PNG. First attempt used LVGL's bundled
// LodePNG decoder (LV_USE_LODEPNG) reading through its stdio filesystem
// driver -- real, back-to-back memory problems on real hardware killed
// that approach:
//   1. LVGL's default allocator (LV_USE_BUILTIN_MALLOC) is its own tiny
//      64KB internal pool, completely separate from ESP-IDF's heap/PSRAM
//      -- a 720x1280 PNG decodes to ~3.6MB (LodePNG always decodes to
//      ARGB8888), so every single decode failed outright. Fixed by
//      switching to LV_USE_CLIB_MALLOC (routes lv_malloc() through plain
//      malloc(), which spills to PSRAM via CONFIG_SPIRAM_USE_MALLOC).
//   2. Even with real PSRAM reachable, decoding needs the raw ARGB8888
//      buffer AND a second, separate draw-buffer-sized copy for the image
//      cache at the same time (~7.2MB combined) -- more than was free
//      (main/tile_cache.c's 128-slot cache alone holds 16MB of PSRAM,
//      unconditionally, whether or not the Map screen is ever opened).
// Raw RGB565 sidesteps both: each image is exactly MAP_LOGICAL_W *
// MAP_LOGICAL_H * 2 bytes (~1.84MB, half of ARGB8888) and needs no
// separate decode-then-cache step -- the file's bytes ARE the pixel data,
// used directly as an LV_IMAGE_SRC_VARIABLE. Even so, holding all 6
// resident at once (~11MB) doesn't reliably fit either, so only ONE
// shared ~1.84MB buffer is kept -- reloaded from whichever mockup's file
// on every LV_EVENT_SCREEN_LOADED, not once per screen up front.
//
// Converted from the original PNGs with a one-off Python/Pillow script
// (not checked in -- see the project memory note on this feature for the
// exact packing used, standard R5G6B5, matching board_lcd_pack_rgb()'s
// convention elsewhere in this project) and copied onto the SD card via
// the Pi's card-reader workflow (tools/sdmount.sh/eject.sh) after the
// console-based sd_xfer path proved too slow for files this size even
// after fixing its correctness bugs (see main/sd_xfer.c's history).
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

#include <stdio.h>

#include "esp_heap_caps.h"
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

static const char *s_raw_paths[MOCKUP_COUNT] = {
    "/sdcard/mockup_home.bin",
    "/sdcard/mockup_map.bin",
    "/sdcard/mockup_nav.bin",
    "/sdcard/mockup_telemetry.bin",
    "/sdcard/mockup_goto.bin",
    "/sdcard/mockup_settings.bin",
};

static lv_obj_t *s_screens[MOCKUP_COUNT];
static lv_obj_t *s_images[MOCKUP_COUNT];

// One shared decode-free image buffer, reloaded from disk on every screen
// switch -- see the file header comment for why holding all 6 resident at
// once doesn't reliably fit in what's left of PSRAM.
#define IMG_DATA_SIZE ((uint32_t)MAP_LOGICAL_W * MAP_LOGICAL_H * 2)
static uint8_t *s_shared_buf;
static lv_image_dsc_t s_shared_dsc = {
    .header = {
        .cf = LV_COLOR_FORMAT_RGB565,
        .w = MAP_LOGICAL_W,
        .h = MAP_LOGICAL_H,
    },
    .data_size = IMG_DATA_SIZE,
};

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

// Reloads the shared buffer from this screen's own file right before it
// becomes visible -- see the file header comment for why this happens on
// every switch rather than once up front.
static void screen_loaded_cb(lv_event_t *e)
{
    mockup_id_t id = (mockup_id_t)(intptr_t)lv_event_get_user_data(e);
    FILE *f = fopen(s_raw_paths[id], "rb");
    if (!f) {
        ESP_LOGE(TAG, "%s: fopen failed", s_raw_paths[id]);
        return;
    }
    size_t n = fread(s_shared_buf, 1, IMG_DATA_SIZE, f);
    fclose(f);
    if (n != IMG_DATA_SIZE) {
        ESP_LOGW(TAG, "%s: short read %u/%u", s_raw_paths[id], (unsigned)n, (unsigned)IMG_DATA_SIZE);
    }

    s_shared_dsc.data = s_shared_buf;
    lv_image_set_src(s_images[id], &s_shared_dsc);
    lv_obj_invalidate(s_images[id]);
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

static lv_obj_t *build_screen(mockup_id_t id)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(screen, screen_loaded_cb, LV_EVENT_SCREEN_LOADED, (void *)(intptr_t)id);

    lv_obj_t *img = lv_image_create(screen);
    lv_obj_set_pos(img, 0, 0);
    s_images[id] = img;

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
    if (!s_shared_buf) {
        s_shared_buf = heap_caps_malloc(IMG_DATA_SIZE, MALLOC_CAP_SPIRAM);
        if (!s_shared_buf) {
            ESP_LOGE(TAG, "couldn't allocate the %u-byte shared image buffer", (unsigned)IMG_DATA_SIZE);
            return;
        }
    }

    ESP_LOGI(TAG, "free heap: internal=%u spiram=%u largest_spiram_block=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

    if (!lvgl_port_lock(0)) {
        ESP_LOGW(TAG, "couldn't take the LVGL lock");
        return;
    }

    if (!s_screens[MOCKUP_HOME]) {
        for (int i = 0; i < MOCKUP_COUNT; i++) {
            s_screens[i] = build_screen((mockup_id_t)i);
        }
        for (int i = 0; i < MOCKUP_COUNT; i++) {
            add_tab_bar(s_screens[i]);
        }

        // Screen-specific extras, on top of the shared tab bar above --
        // see this file's header comment. Coordinates are eyeballed from
        // the mockup images, not pixel-measured like the tab bar.
        make_hotspot(s_screens[MOCKUP_NAV], 20, 70, 680, 110, MOCKUP_GOTO);    // destination header -> Goto
        make_hotspot(s_screens[MOCKUP_NAV], 20, 1050, 330, 90, MOCKUP_MAP);    // "Map View" button -> Map
        make_hotspot(s_screens[MOCKUP_GOTO], 20, 1060, 220, 90, MOCKUP_NAV);   // "Cancel" -> Nav
        make_hotspot(s_screens[MOCKUP_GOTO], 255, 1060, 445, 90, MOCKUP_NAV);  // "Start Navigation" -> Nav
    }

    lv_screen_load(s_screens[MOCKUP_HOME]);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "mockup viewer ready");
}
