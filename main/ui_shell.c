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
// Known limitation, deliberately deferred rather than guessed at: this
// handoff is one-way for now -- map_view.c's render loop has no exit hook,
// so there's no way back to this menu once the map starts. Left as the
// next thing to verify/build once this direction (LVGL shell + native map
// screen) is confirmed sound on real hardware at all.

#include "ui_shell.h"

#include "board_interface.h"
#include "map_config.h"
#include "map_view.h"
#include "touch.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UI_SHELL";

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
    // lvgl_port_add_disp_dsi() (below) registered its own DPI refresh-done
    // callback, silently overwriting board_init()'s -- board_lcd_commit()
    // would hang forever on its very first call otherwise. See the comment
    // on board_lcd_reclaim_refresh_callback() for how this was confirmed.
    board_lcd_reclaim_refresh_callback();
    map_view_start();
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
    lv_obj_align(map_btn, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *settings_btn = make_menu_button(s_screen_main, "Settings", settings_button_cb);
    lv_obj_align(settings_btn, LV_ALIGN_CENTER, 0, 70);
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
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "lvgl_port_add_disp_dsi failed");
        return;
    }

    if (lvgl_port_lock(0)) {
        // Polling indev, not interrupt-driven -- this board's touch IRQ
        // line isn't wired up (same reason main/touch.c itself polls).
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_read_cb);
        lv_indev_set_disp(indev, disp);

        build_main_screen();
        build_settings_screen();
        lvgl_port_unlock();
    }

    xTaskCreate(splash_task, "ui_splash", 4096, NULL, 5, NULL);
}
