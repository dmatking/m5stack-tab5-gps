// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Initialize the ST7123-integrated touch controller on the shared I2C bus.
// Must be called after board_init(). Returns false if init fails (touch disabled).
bool touch_init(void);

typedef struct {
    int16_t x, y;
} touch_point_t;

// Poll for active touch points (0, 1, or 2), writing up to max_points into
// points[] and returning how many are actually down. Coordinates are raw
// panel coordinates (0..board_lcd_width()-1, 0..board_lcd_height()-1).
// Safe to call every loop iteration -- this is a plain register read, not
// an event queue.
uint8_t touch_poll_multi(touch_point_t *points, uint8_t max_points);
