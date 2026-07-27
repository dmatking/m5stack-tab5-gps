// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <stdbool.h>
#include <stdint.h>

// Initialize the ST7123-integrated touch controller on the shared I2C bus.
// Must be called after board_init(). Returns false if init fails (touch disabled).
bool touch_init(void);

// Poll for a touch point. Returns true if a finger is currently down, and writes
// the raw panel coordinates (0..board_lcd_width()-1, 0..board_lcd_height()-1)
// to *x, *y. Safe to call every loop iteration — this is a plain register read,
// not an event queue.
bool touch_poll(int16_t *x, int16_t *y);
