// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Pan state + the touch-drag -> render loop for the map view. Touch is
// polled once per render iteration (a single I2C read, not a bottleneck);
// tile *production* is decoupled onto tile_cache's own generator task, so
// this loop never blocks on it -- only PPA blits/fills, which are hardware
// DMA operations, sit on the critical path.

#include "map_view.h"
#include "board_interface.h"
#include "map_config.h"
#include "tile_cache.h"
#include "touch.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAP_VIEW";

static void map_task(void *arg)
{
    (void)arg;

    // Start centered on the embedded real-tile grid (see map_config.h) rather
    // than world tile (0,0) -- otherwise the real tiles are unreachable
    // without dragging millions of pixels to get there.
    int32_t grid_w = MAP_TILE_GRID_COLS * MAP_TILE_SIZE;
    int32_t grid_h = MAP_TILE_GRID_ROWS * MAP_TILE_SIZE;
    int32_t pan_x = MAP_TILE_BASE_TX * MAP_TILE_SIZE + (grid_w - board_lcd_width()) / 2;
    int32_t pan_y = MAP_TILE_BASE_TY * MAP_TILE_SIZE + (grid_h - board_lcd_height()) / 2;

    bool dragging = false;
    int16_t last_x = 0, last_y = 0;

    const TickType_t period = pdMS_TO_TICKS(1000 / MAP_RENDER_FPS);
    TickType_t next_wake = xTaskGetTickCount();

    // Measures the actual back-to-back cost of a redraw, counting only the
    // gaps between frames that *did* redraw -- idle ticks (skipped by the
    // dirty-check in tile_cache_render_viewport) don't dilute this, unlike a
    // plain wall-clock window average would if the drag gesture has pauses.
    int64_t last_redraw_us = -1;
    int64_t redraw_interval_sum_us = 0;
    uint32_t redraw_interval_count = 0;
    uint32_t ticks_since_log = 0;

    while (1) {
        ticks_since_log++;

        int16_t x, y;
        bool pressed = touch_poll(&x, &y);

        if (pressed) {
            if (dragging) {
                pan_x -= (int32_t)(x - last_x);
                pan_y -= (int32_t)(y - last_y);
            }
            last_x = x;
            last_y = y;
            dragging = true;
        } else {
            dragging = false;
        }

        if (tile_cache_render_viewport(pan_x, pan_y)) {
            board_lcd_commit();

            int64_t now = esp_timer_get_time();
            if (last_redraw_us >= 0) {
                redraw_interval_sum_us += now - last_redraw_us;
                redraw_interval_count++;
            }
            last_redraw_us = now;

            if (redraw_interval_count >= 30) {
                double avg_ms = (redraw_interval_sum_us / (double)redraw_interval_count) / 1000.0;
                ESP_LOGI(TAG, "redraw: %.1f ms avg (%.1f fps) over last %u redraws / %u loop ticks, pan %ld,%ld",
                         avg_ms, 1000.0 / avg_ms, redraw_interval_count, ticks_since_log, (long)pan_x, (long)pan_y);
                redraw_interval_sum_us = 0;
                redraw_interval_count = 0;
                ticks_since_log = 0;
            }
        }

        vTaskDelayUntil(&next_wake, period);
    }
}

void map_view_start(void)
{
    xTaskCreatePinnedToCore(map_task, "map_view", 4096, NULL, 5, NULL, 0);
    ESP_LOGI(TAG, "map view started");
}
