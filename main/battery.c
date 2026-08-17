// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0

#include "battery.h"

#include "board_interface.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define INA226_ADDR       0x41
#define INA226_REG_BUSV   0x02  // bus (pack) voltage, 1.25 mV/bit, no calibration needed
#define I2C_TIMEOUT_MS    50

static const char *TAG = "BATTERY";

static i2c_master_dev_handle_t s_dev;
static bool s_present;

// Raw INA226 bus-voltage register -> volts. Shared by battery_init()'s probe
// and battery_read() so there's exactly one place doing the 1.25
// mV/bit math (see INA226_REG_BUSV's own comment).
static bool read_bus_voltage(float *out_volts)
{
    if (!s_dev) return false;
    uint8_t reg = INA226_REG_BUSV;
    uint8_t rb[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, rb, 2, I2C_TIMEOUT_MS);
    if (err != ESP_OK) return false;
    uint16_t raw = ((uint16_t)rb[0] << 8) | rb[1];
    *out_volts = raw * 0.00125f;
    return true;
}

void battery_init(void)
{
    i2c_master_bus_handle_t bus = board_i2c_bus_handle();
    if (!bus) {
        ESP_LOGW(TAG, "no I2C bus (board has none, or board_init() hasn't run) -- battery gauge disabled");
        return;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = INA226_ADDR,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "i2c_master_bus_add_device failed -- battery gauge disabled");
        s_dev = NULL;
        return;
    }

    float volts;
    s_present = read_bus_voltage(&volts);
    if (s_present) {
        ESP_LOGI(TAG, "INA226 present at 0x%02X, pack voltage %.2f V", INA226_ADDR, volts);
    } else {
        ESP_LOGW(TAG, "INA226 not responding at 0x%02X -- battery gauge disabled", INA226_ADDR);
    }
}

bool battery_present(void)
{
    return s_present;
}

// Below this, a reading isn't a real (even critically depleted) 2S pack --
// it's the INA226's bus-voltage pin floating/pulled with no battery
// installed. A protected 2S Li-ion pack's own protection IC disconnects
// around 2.5V/cell (~5.0V pack) to prevent over-discharge damage, so any
// genuinely connected pack should read well above this floor under any
// real condition; picked with headroom below that, not derived from a
// measurement of this board's actual no-battery reading (that would need
// physically pulling the pack and logging the raw value -- ESP_LOGI below
// does exactly that so it's easy to check/tune from a real capture rather
// than trusting this blind).
#define BATTERY_NO_PACK_VOLTS_FLOOR 4.0f

bool battery_read(int *out_percent, bool *out_external)
{
    if (!s_present || !out_percent || !out_external) return false;

    float pack_volts;
    if (!read_bus_voltage(&pack_volts)) return false;

    if (pack_volts < BATTERY_NO_PACK_VOLTS_FLOOR) {
        ESP_LOGI(TAG, "bus voltage %.2f V < %.2f V floor -- no pack installed, on external power",
                 pack_volts, BATTERY_NO_PACK_VOLTS_FLOOR);
        *out_percent = 0;
        *out_external = true;
        return true;
    }

    // Same curve M5Unified's Power_Class uses for this board (confirmed via
    // its own source): halve the 2S pack voltage to per-cell millivolts,
    // then a standard Li-ion 3300-4150 mV/cell -> 0-100% linear map.
    float cell_mv = (pack_volts * 1000.0f) / 2.0f;
    float pct = (cell_mv - 3300.0f) * 100.0f / 850.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    *out_percent = (int)(pct + 0.5f);
    *out_external = false;
    ESP_LOGI(TAG, "bus voltage %.3f V -> %d%%", pack_volts, *out_percent);
    return true;
}
