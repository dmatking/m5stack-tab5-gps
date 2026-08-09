// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// LVGL app shell: splash -> main screen (Map / Settings buttons) -> either
// a placeholder Settings screen (pure LVGL, round-trips back to the main
// screen freely) or the real Map screen.
//
// The Map screen is NOT an LVGL screen -- it's the existing native PPA
// tile-compositor path (main/map_view.c/tile_cache.c), which already owns
// the panel's hardware framebuffers directly (board_lcd_hw_framebuffer()/
// board_lcd_commit(), with its own VSYNC-gated flip). Rather than trying to
// make LVGL and that path cooperate on the same buffers at the same time --
// two independent flip-trackers fighting over which physical buffer is
// "current" is exactly the kind of thing that goes wrong only on real
// hardware -- they take turns: entering the Map screen calls
// lvgl_port_stop() (halts LVGL's render/flush timer, so it stops touching
// the panel at all) and then starts the native map task, which takes over
// completely from there.
//
// The Map screen exits back to this menu via a swipe-up-from-the-bottom-
// edge gesture (see main/map_view.c -> ui_shell_return_to_menu(), below).
//
// Earlier version of this file tried to make the two sides "hand off" the
// DPI panel's on_refresh_done/on_color_trans_done callback registration to
// each other on every transition (steal it entering the map, give it back
// leaving it). That's real, and really was needed -- LVGL's own flush-ready
// signal in this display mode is delivered through on_color_trans_done, and
// esp_lvgl_port re-registering it on init silently clobbers board_init()'s
// on_refresh_done registration (board_lcd_commit() blocks on that one) --
// but doing it as a hand-off, live, is fragile: it locked up on the very
// first swipe-back on real hardware (LVGL's own wait_for_flushing() stuck
// forever, confirmed via addr2line against the crash dump). The two
// callbacks are for genuinely different, non-conflicting events (a
// per-frame VSYNC tick our board_lcd_commit() needs vs. a per-flush
// color-copy-done signal LVGL needs, fired from completely different
// contexts -- ISR vs. synchronous task-context), so there's no real reason
// they can't both just stay registered all the time. Fixed the right way:
// register both together, ONCE (board_lcd_register_color_trans_done_cb(),
// called below), and never touch the registration again in either
// direction -- see its comment in board_interface.h for the full story.

#include "ui_shell.h"

#include "board_interface.h"
#include "map_config.h"
#include "map_view.h"
#include "mockup_viewer.h"
#include "touch.h"

#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UI_SHELL";

static lv_display_t *s_disp;
static lv_obj_t *s_screen_main;
static lv_obj_t *s_screen_settings;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    touch_point_t pt;
    if (touch_poll_multi(&pt, 1) >= 1) {
        data->point.x = pt.x;
        data->point.y = pt.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void map_button_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "Map selected -- stopping LVGL and handing the panel to the native map renderer");
    lvgl_port_stop();
    map_view_start();
}

// Mirrors esp_lvgl_port's own (private) DPI on_color_trans_done callback --
// see the file header comment for why this is registered permanently
// alongside board_init()'s on_refresh_done rather than fought over.
static bool dsi_flush_ready_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel; (void)edata;
    lv_disp_flush_ready((lv_display_t *)user_ctx);
    return false;
}

void ui_shell_return_to_menu(void)
{
    ESP_LOGI(TAG, "Returning to the menu");
    lvgl_port_resume();

    if (lvgl_port_lock(0)) {
        lv_screen_load(s_screen_main);
        // The physical framebuffer was fully overwritten by the native map
        // renderer while LVGL was stopped -- LVGL's own dirty-tracking has
        // no way to know that happened, so it still thinks s_screen_main is
        // already loaded and unchanged since last drawn and skips
        // redrawing most of it. Force it to redraw for real.
        lv_obj_invalidate(s_screen_main);
        lvgl_port_unlock();
    }
}

void ui_shell_show_main_menu(void)
{
    if (lvgl_port_lock(0)) {
        lv_screen_load(s_screen_main);
        lvgl_port_unlock();
    }
}

static void mockups_button_cb(lv_event_t *e)
{
    (void)e;
    mockup_viewer_start();
}

static void settings_button_cb(lv_event_t *e)
{
    (void)e;
    lv_screen_load(s_screen_settings);
}

static void settings_back_cb(lv_event_t *e)
{
    (void)e;
    lv_screen_load(s_screen_main);
}

static lv_obj_t *make_menu_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 400, 100);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_center(label);
    return btn;
}

static void build_main_screen(void)
{
    s_screen_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen_main, lv_color_black(), 0);

    lv_obj_t *title = lv_label_create(s_screen_main);
    lv_label_set_text(title, "GPS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *map_btn = make_menu_button(s_screen_main, "Map", map_button_cb);
    lv_obj_align(map_btn, LV_ALIGN_CENTER, 0, -140);

    lv_obj_t *settings_btn = make_menu_button(s_screen_main, "Settings", settings_button_cb);
    lv_obj_align(settings_btn, LV_ALIGN_CENTER, 0, 0);

    // Visual-design mockups, not a real feature -- see main/mockup_viewer.c.
    lv_obj_t *mockups_btn = make_menu_button(s_screen_main, "Mockups", mockups_button_cb);
    lv_obj_align(mockups_btn, LV_ALIGN_CENTER, 0, 140);
}

static void build_settings_screen(void)
{
    s_screen_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen_settings, lv_color_black(), 0);

    lv_obj_t *label = lv_label_create(s_screen_settings);
    lv_label_set_text(label, "Settings\n(nothing here yet)");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);

    lv_obj_t *back_btn = make_menu_button(s_screen_settings, "Back", settings_back_cb);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -60);
}

// Splash needs a plain delay before switching screens -- runs on its own
// short-lived task so ui_shell_start() (called from app_main) can return
// immediately instead of blocking startup on a fixed timer.
static void splash_task(void *arg)
{
    (void)arg;
    lv_obj_t *splash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(splash, lv_color_black(), 0);

    lv_obj_t *label = lv_label_create(splash);
    lv_label_set_text(label, "GPS");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    if (lvgl_port_lock(0)) {
        lv_screen_load(splash);
        lvgl_port_unlock();
    }

    vTaskDelay(pdMS_TO_TICKS(1500));

    if (lvgl_port_lock(0)) {
        lv_screen_load(s_screen_main);
        lv_obj_delete(splash);
        lvgl_port_unlock();
    }

    vTaskDelete(NULL);
}

void ui_shell_start(void)
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    // Direct-to-DPI-framebuffer mode, same as the map's own path: the DPI
    // panel already owns a PSRAM framebuffer the controller scans
    // continuously, so esp_lvgl_port renders straight into it -- no
    // flush_cb of our own. avoid_tearing=false matches what's been
    // verified working on this exact board/panel elsewhere (see
    // m5stack-tab5-lvgl-primer); this app is portrait-native already
    // (see map_config.h), so no rotation is needed here either.
    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = board_lcd_panel_handle(),
        .buffer_size = MAP_LOGICAL_W * MAP_LOGICAL_H,
        .double_buffer = true,
        .hres = MAP_LOGICAL_W,
        .vres = MAP_LOGICAL_H,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = {
        .flags = { .avoid_tearing = false },
    };
    s_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!s_disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_dsi failed");
        return;
    }

    // lvgl_port_add_disp_dsi() just registered its own on_color_trans_done
    // callback directly, clobbering board_init()'s on_refresh_done in the
    // process (single-call registration API, see board_interface.h's
    // comment on this function). Fix it up once, permanently -- re-registers
    // both together so board_lcd_commit() and LVGL's flush-ready signal both
    // keep working from here on, with no further hand-off needed in either
    // direction (see the file header comment for why the earlier live
    // hand-off approach was fragile and is gone).
    board_lcd_register_color_trans_done_cb(dsi_flush_ready_cb, s_disp);

    if (lvgl_port_lock(0)) {
        // Polling indev, not interrupt-driven -- this board's touch IRQ
        // line isn't wired up (same reason main/touch.c itself polls).
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_disp(indev, s_disp);

        build_main_screen();
        build_settings_screen();
        lvgl_port_unlock();
    }

    xTaskCreate(splash_task, "ui_splash", 4096, NULL, 5, NULL);
}
