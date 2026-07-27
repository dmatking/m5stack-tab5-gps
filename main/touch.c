// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Touch input for the M5Stack Tab5's ST7123-integrated touch controller.
// I2C addr 0x55, INT on GPIO23, RST not connected (PI4IOE manages power-on).
// Config mirrors the proven working setup in m5stack-tab5-video-stream/main/lv_port.c,
// minus the LVGL glue — we read coordinates directly.

#include "touch.h"
#include "board_interface.h"

#include "driver/gpio.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_log.h"

static const char *TAG = "TOUCH";
static esp_lcd_touch_handle_t s_tp = NULL;

#define ST7123_TOUCH_I2C_ADDR  0x55
#define ST7123_TOUCH_INT_GPIO  GPIO_NUM_23

bool touch_init(void)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr              = ST7123_TOUCH_I2C_ADDR,
        .scl_speed_hz          = 400000,
        .control_phase_bytes   = 1,
        .dc_bit_offset         = 0,
        .lcd_cmd_bits          = 16,
        .flags.disable_control_phase = 1,
    };
    esp_err_t err = esp_lcd_new_panel_io_i2c(board_i2c_bus_handle(), &tp_io_cfg, &tp_io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "touch panel IO init failed (0x%x) -- touch disabled", err);
        return false;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max        = board_lcd_width(),
        .y_max        = board_lcd_height(),
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = ST7123_TOUCH_INT_GPIO,
        .levels       = { .reset = 0, .interrupt = 0 },
        .flags        = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
    };
    err = esp_lcd_touch_new_i2c_st7123(tp_io, &tp_cfg, &s_tp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ST7123 touch init failed (0x%x) -- touch disabled", err);
        s_tp = NULL;
        return false;
    }

    ESP_LOGI(TAG, "ST7123 touch ready @ I2C 0x%02x", ST7123_TOUCH_I2C_ADDR);
    return true;
}

uint8_t touch_poll_multi(touch_point_t *points, uint8_t max_points)
{
    if (!s_tp || max_points == 0) return 0;

    esp_lcd_touch_read_data(s_tp);

    uint16_t tx[2], ty[2];
    uint8_t cnt = 0;
    uint8_t query_max = max_points > 2 ? 2 : max_points;
    bool pressed = esp_lcd_touch_get_coordinates(s_tp, tx, ty, NULL, &cnt, query_max);
    if (!pressed || cnt == 0) return 0;

    if (cnt > query_max) cnt = query_max;
    for (uint8_t i = 0; i < cnt; i++) {
        points[i].x = (int16_t)tx[i];
        points[i].y = (int16_t)ty[i];
    }
    return cnt;
}
