// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// LVGL app shell: splash -> the real design UI (main/design_ui.c and
// friends -- Home screen + tab bar). Superseded the original placeholder
// 3-button menu (Map/Settings/Mockups) once real Home/Map screens landed
// 2026-08-09 -- see main/mockup_viewer.c, now orphaned/unreferenced (its
// whole purpose was previewing the design before real screens existed).
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
// The Map screen exits back to LVGL via its own native navbar (a mirror of
// the real one, see main/ui_overlay.c's ui_overlay_draw_navbar()) -- tapping
// any tab but Map calls ui_shell_return_to_tab(), below, and switches
// straight to it. (This used to be a swipe-up-from-the-bottom-edge gesture
// that only reached Home -- replaced once the navbar existed, since the
// navbar is strictly better: a visible affordance instead of a hidden
// gesture, and it reaches all 5 tabs.)
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

#include "app_settings.h"
#include "board_interface.h"
#include "design_ui.h"
#include "navbar_snapshot.h"
#include "map_config.h"
#include "map_view.h"
#include "touch.h"

#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "UI_SHELL";

static lv_display_t *s_disp;

// Same screen-off idle timeout as the Map screen (main/map_view.c),
// reusing its exact constant/value (MAP_SCREEN_TIMEOUT_US, map_config.h)
// rather than a second copy of the same setting -- touch activity only,
// deliberately not GPS/background updates, same reasoning as map_view.c's
// own version: the screen would never time out on its own otherwise,
// since GPS keeps streaming fixes regardless of whether anyone's looking.
// Implemented as an lv_timer (not a plain FreeRTOS task) specifically so
// it naturally pauses itself while the Map screen is up -- lvgl_port_stop()
// halts LVGL's whole timer subsystem then, so this stops ticking right
// along with everything else and can't fight with map_view.c's own
// independent timeout logic over the same backlight.
static int64_t s_last_activity_us;
static bool s_lvgl_screen_on = true;
static bool s_map_active = false; // true while the native map renderer owns the panel (see ui_shell_map_active())

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    touch_point_t pt;
    bool pressed = touch_poll_multi(&pt, 1) >= 1;
    if (pressed) {
        s_last_activity_us = esp_timer_get_time();
        if (!s_lvgl_screen_on) {
            // Waking from an idle timeout consumes this touch -- same
            // convention as map_view.c's own wake handling -- so it
            // doesn't also land on whatever happened to be under the
            // finger the moment the screen came back on.
            board_lcd_set_backlight(true);
            s_lvgl_screen_on = true;
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        data->point.x = pt.x;
        data->point.y = pt.y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void idle_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_lvgl_screen_on) return; // already off, waiting on touch_read_cb() to wake it
    if (MAP_SCREEN_TIMEOUT_US <= 0) return;
    if (app_settings_get_keep_screen_on()) return; // Settings' "Keep screen on" -- see map_view.c for its own copy of this check
    if (esp_timer_get_time() - s_last_activity_us > MAP_SCREEN_TIMEOUT_US) {
        board_lcd_set_backlight(false);
        s_lvgl_screen_on = false;
    }
}

// Called from design_ui.c's ui_show_tab() when the MAP tab is selected --
// ui_map's own screen is a decorative placeholder (schematic grid, demo
// route/track lines, no real cartography, see ui_map.c's header comment),
// not meant to actually be shown. This hands off to the native renderer
// exactly like the old placeholder menu's "Map" button did.
void ui_shell_enter_map(void)
{
    ESP_LOGI(TAG, "Map selected -- stopping LVGL and handing the panel to the native map renderer");
    s_map_active = true;
    lvgl_port_stop();
    map_view_start();
}

// Lets other subsystems (main/fb_capture.c) tell whether lv_screen_active()
// currently reflects what's actually on the panel -- it doesn't while the
// Map screen is up (LVGL is stopped, see ui_shell_enter_map() above, and
// lv_screen_active() just keeps returning whatever screen was loaded before
// the handoff).
bool ui_shell_map_active(void) { return s_map_active; }

// Mirrors esp_lvgl_port's own (private) DPI on_color_trans_done callback --
// see the file header comment for why this is registered permanently
// alongside board_init()'s on_refresh_done rather than fought over.
static bool dsi_flush_ready_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel; (void)edata;
    lv_disp_flush_ready((lv_display_t *)user_ctx);
    return false;
}

void ui_shell_return_to_tab(int tab_index)
{
    // tab_index matches main/ui_overlay.c's own local ordering (see its doc
    // comment in ui_overlay.h) -- kept as a plain int across this boundary
    // so the native map/overlay code doesn't need to pull in design_ui.h's
    // (LVGL-typed) ui_tab_t. Keep this array in sync by hand if ui_tab_t's
    // tab set ever changes.
    static const ui_tab_t tabs[] = {
        UI_TAB_HOME, UI_TAB_MAP, UI_TAB_NAV, UI_TAB_TELEMETRY, UI_TAB_MORE,
    };
    if (tab_index < 0 || tab_index >= (int)(sizeof(tabs) / sizeof(tabs[0]))) {
        ESP_LOGW(TAG, "ui_shell_return_to_tab: bad tab_index %d, defaulting to Home", tab_index);
        tab_index = 0;
    }

    ESP_LOGI(TAG, "Returning to tab %d from the map", tab_index);
    // The LVGL-side idle timer (see touch_read_cb()/idle_timer_cb() above)
    // was paused this whole time -- lvgl_port_stop() halts LVGL's timer
    // subsystem, so it simply stopped ticking rather than tracking
    // anything wrong. But s_last_activity_us is still whatever it was
    // right before the Map screen took over, possibly minutes ago; reset
    // both here so the timer doesn't immediately think the screen's been
    // idle since then the moment it resumes. (The physical backlight is
    // already on: map_view.c only lets a navbar tap like this one reach
    // ui_shell_return_to_tab() at all while its own screen-on is true --
    // an idle Map screen consumes the first tap to wake itself instead.)
    s_last_activity_us = esp_timer_get_time();
    s_lvgl_screen_on = true;
    s_map_active = false;
    lvgl_port_resume();

    if (lvgl_port_lock(0)) {
        ui_show_tab(tabs[tab_index]);
        // The physical framebuffer was fully overwritten by the native map
        // renderer while LVGL was stopped -- LVGL's own dirty-tracking has
        // no way to know that happened, so it still thinks the destination
        // screen is already loaded and unchanged since last drawn and skips
        // redrawing most of it. Force it to redraw for real.
        lv_obj_invalidate(lv_screen_active());
        lvgl_port_unlock();
    }
}

void ui_shell_show_main_menu(void)
{
    if (lvgl_port_lock(0)) {
        lv_screen_load(ui_home()->screen);
        lvgl_port_unlock();
    }
}

// Builds and loads the splash screen. Called synchronously from
// ui_shell_start(), under lock, BEFORE ui_init() builds the real screens --
// it used to run on splash_task instead, after ui_init() already loaded
// Home as its own last step, which raced: default LVGL screen -> Home
// (flashed on by ui_init()) -> splash (loaded a beat later here) -> Home
// again. Loading splash first, before anything else exists to flash by,
// fixes that -- ui_init() no longer loads any screen at all now.
static lv_obj_t *create_and_load_splash(void)
{
    lv_obj_t *splash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(splash, lv_color_black(), 0);

    lv_obj_t *label = lv_label_create(splash);
    lv_label_set_text(label, "GPS");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    lv_screen_load(splash);
    return splash;
}

// Splash needs a plain delay before switching to Home -- runs on its own
// short-lived task so ui_shell_start() (called from app_main) can return
// immediately instead of blocking startup on a fixed timer.
static void splash_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(1500));

    if (lvgl_port_lock(0)) {
        lv_screen_load(ui_home()->screen);
        // Deliberately NOT calling lv_obj_delete(splash) here anymore --
        // hit a real crash on real hardware (Guru Meditation Error, Load
        // access fault deep inside LVGL's own lv_event_mark_deleted(),
        // confirmed via addr2line against the exact PC/stack). Only
        // reproduced once in a handful of boots, right around the same
        // ~1.5s mark this delete always runs at, after gps_ui_bridge.c's
        // periodic task started competing for the same LVGL lock -- never
        // fully root-caused (would need a debugger, not just log
        // archaeology), but a splash screen is cheap enough to just leak
        // forever rather than risk it. A few hundred bytes, once, for the
        // life of the app -- not worth chasing a heisenbug over tonight.
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

        s_last_activity_us = esp_timer_get_time();
        lv_timer_create(idle_timer_cb, 1000, NULL);

        // Splash first, before anything else exists to flash by -- see
        // create_and_load_splash()'s comment. Only after it's the active
        // screen do we pay for building the real (heavier) 6-screen UI.
        create_and_load_splash();
        ui_init();
        // Captures a real, LVGL-rendered copy of the navbar (Map tab
        // active) for the native Map screen to blit later -- see
        // navbar_snapshot.h for why this replaced an earlier attempt at
        // live-rendering real fonts/icons directly from the native path
        // (it crashed real hardware). Needs a live default display and
        // ui_theme_init() already done, both true by this point.
        navbar_snapshot_capture();
        lvgl_port_unlock();
    }

    xTaskCreate(splash_task, "ui_splash", 4096, NULL, 5, NULL);
}
