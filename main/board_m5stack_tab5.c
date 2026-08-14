// Copyright 2025-2026 David M. King
// SPDX-License-Identifier: Apache-2.0
//
// Board implementation for M5Stack Tab5 (ESP32-P4)
// Post-Oct-2025 hardware revision: ST7123 combined display+touch IC
// 720x1280 MIPI-DSI 2-lane, RGB565
//
// Init sequence derived from M5Tab5-UserDemo (MIT, M5Stack Technology CO LTD)
// ST7123 LCD driver: Apache-2.0, Espressif Systems

#include "board_interface.h"

#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7123.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "BOARD_TAB5";

#define BOARD_NAME  "M5Stack Tab5"

// I2C (system bus — PI4IOE expanders and touch)
#define I2C_BUS_NUM     0
#define I2C_SDA_GPIO    31
#define I2C_SCL_GPIO    32
#define I2C_TIMEOUT_MS  50

// PI4IOE5V6408 IO expander I2C addresses
#define PI4IOE1_ADDR    0x43  // addr pin low
#define PI4IOE2_ADDR    0x44  // addr pin high

// PI4IOE register map
#define PI4IO_REG_CHIP_RESET  0x01
#define PI4IO_REG_IO_DIR      0x03
#define PI4IO_REG_OUT_SET     0x05
#define PI4IO_REG_OUT_H_IM    0x07
#define PI4IO_REG_IN_DEF_STA  0x09
#define PI4IO_REG_PULL_EN     0x0B
#define PI4IO_REG_PULL_SEL    0x0D
#define PI4IO_REG_INT_MASK    0x11

// LDO channel for MIPI DSI PHY power
#define DSI_PHY_LDO_CHAN        3
#define DSI_PHY_LDO_VOLTAGE_MV  2500

// Backlight: LEDC PWM on GPIO 22
#define LCD_BACKLIGHT_GPIO  22
#define LCD_LEDC_TIMER      LEDC_TIMER_0
#define LCD_LEDC_CHAN       LEDC_CHANNEL_1
#define LCD_LEDC_FREQ_HZ    5000
#define LCD_LEDC_DUTY_RES   LEDC_TIMER_12_BIT
#define LCD_LEDC_DUTY_MAX   4095

// Display geometry and DSI parameters
#define LCD_W               720
#define LCD_H               1280
#define BPP                 2   // RGB565
#define FB_SIZE             (LCD_W * LCD_H * BPP)
#define DSI_LANE_BITRATE    965  // Mbps
#define DPI_CLOCK_MHZ       70

static esp_lcd_panel_handle_t    s_panel     = NULL;
static esp_lcd_panel_io_handle_t s_panel_io  = NULL;
static uint8_t                  *s_fbs[2]    = {0};  // hardware double-buffers (DPI)
static int                       s_back_idx  = 0;    // index of the back (render) buffer
static uint8_t                  *s_backbuf   = NULL; // software render buffer (PSRAM)

// Given from the DPI driver's on_refresh_done callback each time a committed
// buffer finishes actually being scanned out over DSI -- see board_lcd_commit().
static SemaphoreHandle_t s_refresh_done_sem = NULL;

// Fires once per completed DSI scan-out of whatever buffer is currently
// being displayed -- the panel refreshes continuously at its own fixed rate
// regardless of whether a new buffer was just committed, so this can fire
// even when nothing changed. board_lcd_commit() waits on the resulting
// semaphore before returning, which is what actually matters: it guarantees
// the render loop never starts compositing into a buffer the hardware is
// still actively scanning (see board_lcd_commit()'s comment for the failure
// mode this closes -- a real, reproduced-on-hardware localized stutter in
// whichever screen region happened to be written earliest in the composite
// pass and land latest in DSI scan order).
static bool dpi_refresh_done_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    (void)panel; (void)edata; (void)user_ctx;
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_refresh_done_sem, &hp_task_woken);
    return hp_task_woken == pdTRUE;
}

static i2c_master_bus_handle_t s_i2c_bus     = NULL;  // shared I2C bus 0 (IO expanders + touch)
static i2c_master_dev_handle_t s_pi4ioe2_dev = NULL;  // kept around for board_set_usb5v_en()

// --- ST7123 vendor init sequence (M5Stack Tab5, post-Oct-2025 hardware) ---
static const st7123_lcd_init_cmd_t s_st7123_init[] = {
    {0x60, (uint8_t[]){0x71, 0x23, 0xa2}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa3}, 3, 0},
    {0x60, (uint8_t[]){0x71, 0x23, 0xa4}, 3, 0},
    {0xA4, (uint8_t[]){0x31}, 1, 0},
    {0xD7, (uint8_t[]){0x10, 0x0A, 0x10, 0x2A, 0x80, 0x80}, 6, 0},
    {0x90, (uint8_t[]){0x71, 0x23, 0x5A, 0x20, 0x24, 0x09, 0x09}, 7, 0},
    {0xA3, (uint8_t[]){0x80, 0x01, 0x88, 0x30, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x00, 0x00,
                       0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x4F, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46,
                       0x00, 0x00, 0x1E, 0x5C, 0x1E, 0x80, 0x00, 0x6F, 0x58, 0x00, 0x00, 0x00, 0xFF},
     40, 0},
    {0xA6, (uint8_t[]){0x03, 0x00, 0x24, 0x55, 0x36, 0x00, 0x39, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24,
                       0x55, 0x38, 0x00, 0x37, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0x24, 0x11, 0x00, 0x00,
                       0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x00, 0xEC, 0x11, 0x00, 0x03, 0x00, 0x03, 0x6E,
                       0x6E, 0xFF, 0xFF, 0x00, 0x08, 0x80, 0x08, 0x80, 0x06, 0x00, 0x00, 0x00, 0x00},
     55, 0},
    {0xA7, (uint8_t[]){0x19, 0x19, 0x80, 0x64, 0x40, 0x07, 0x16, 0x40, 0x00, 0x44, 0x03, 0x6E, 0x6E, 0x91, 0xFF,
                       0x08, 0x80, 0x64, 0x40, 0x25, 0x34, 0x40, 0x00, 0x02, 0x01, 0x6E, 0x6E, 0x91, 0xFF, 0x08,
                       0x80, 0x64, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x6E, 0x6E, 0x91, 0xFF, 0x08, 0x80,
                       0x64, 0x40, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x6E, 0x6E, 0x84, 0xFF, 0x08, 0x80, 0x44},
     60, 0},
    {0xAC, (uint8_t[]){0x03, 0x19, 0x19, 0x18, 0x18, 0x06, 0x13, 0x13, 0x11, 0x11, 0x08, 0x08, 0x0A, 0x0A, 0x1C,
                       0x1C, 0x07, 0x07, 0x00, 0x00, 0x02, 0x02, 0x01, 0x19, 0x19, 0x18, 0x18, 0x06, 0x12, 0x12,
                       0x10, 0x10, 0x09, 0x09, 0x0B, 0x0B, 0x1C, 0x1C, 0x07, 0x07, 0x03, 0x03, 0x01, 0x01},
     44, 0},
    {0xAD, (uint8_t[]){0xF0, 0x00, 0x46, 0x00, 0x03, 0x50, 0x50, 0xFF, 0xFF, 0xF0, 0x40, 0x06, 0x01,
                       0x07, 0x42, 0x42, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF},
     25, 0},
    {0xAE, (uint8_t[]){0xFE, 0x3F, 0x3F, 0xFE, 0x3F, 0x3F, 0x00}, 7, 0},
    {0xB2, (uint8_t[]){0x15, 0x19, 0x05, 0x23, 0x49, 0xAF, 0x03, 0x2E, 0x5C, 0xD2, 0xFF, 0x10, 0x20,
                       0xFD, 0x20, 0xC0, 0x00}, 17, 0},
    {0xE8, (uint8_t[]){0x20, 0x6F, 0x04, 0x97, 0x97, 0x3E, 0x04, 0xDC, 0xDC, 0x3E, 0x06, 0xFA, 0x26, 0x3E}, 14, 0},
    {0x75, (uint8_t[]){0x03, 0x04}, 2, 0},
    {0xE7, (uint8_t[]){0x3B, 0x00, 0x00, 0x7C, 0xA1, 0x8C, 0x20, 0x1A, 0xF0, 0xB1, 0x50, 0x00,
                       0x50, 0xB1, 0x50, 0xB1, 0x50, 0xD8, 0x00, 0x55, 0x00, 0xB1, 0x00, 0x45,
                       0xC9, 0x6A, 0xFF, 0x5A, 0xD8, 0x18, 0x88, 0x15, 0xB1, 0x01, 0x01, 0x77},
     36, 0},
    {0xEA, (uint8_t[]){0x13, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x2C}, 8, 0},
    {0xB0, (uint8_t[]){0x22, 0x43, 0x11, 0x61, 0x25, 0x43, 0x43}, 7, 0},
    {0xb7, (uint8_t[]){0x00, 0x00, 0x73, 0x73}, 4, 0},
    {0xBF, (uint8_t[]){0xA6, 0xAA}, 2, 0},
    {0xA9, (uint8_t[]){0x00, 0x00, 0x73, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03}, 10, 0},
    {0xC8, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0xC9, (uint8_t[]){0x00, 0x00, 0x10, 0x1F, 0x36, 0x00, 0x5D, 0x04, 0x9D, 0x05, 0x10, 0xF2, 0x06,
                       0x60, 0x03, 0x11, 0xAD, 0x00, 0xEF, 0x01, 0x22, 0x2E, 0x0E, 0x74, 0x08, 0x32,
                       0xDC, 0x09, 0x33, 0x0F, 0xF3, 0x77, 0x0D, 0xB0, 0xDC, 0x03, 0xFF},
     37, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x11, (uint8_t[]){0x00}, 1, 100},
    {0x29, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 100},
};

// --- PI4IOE5V6408 IO expander init ---
// Two chips: PI4IOE1 (0x43) controls LCD_RST, TP_RST, SPK_EN, EXT5V, CAM_RST
//            PI4IOE2 (0x44) controls WLAN_PWR, USB5V, CHG_EN
static void pi4ioe_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source              = I2C_CLK_SRC_DEFAULT,
        .i2c_port                = I2C_BUS_NUM,
        .sda_io_num              = I2C_SDA_GPIO,
        .scl_io_num              = I2C_SCL_GPIO,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_i2c_bus));

    i2c_master_dev_handle_t dev1, dev2;
    i2c_device_config_t dev_cfg1 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PI4IOE1_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg1, &dev1));

    uint8_t wb[2], rb[1];

    // PI4IOE1: P0=NC, P1=SPK_EN, P2=EXT5V_EN, P3=NC, P4=LCD_RST, P5=TP_RST, P6=CAM_RST, P7=IN
    wb[0] = PI4IO_REG_CHIP_RESET; wb[1] = 0xFF;
    i2c_master_transmit(dev1, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_CHIP_RESET;
    i2c_master_transmit_receive(dev1, wb, 1, rb, 1, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_IO_DIR; wb[1] = 0b01111111;  // P0-P6 output, P7 input
    i2c_master_transmit(dev1, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_OUT_H_IM; wb[1] = 0b00000000;
    i2c_master_transmit(dev1, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_PULL_SEL; wb[1] = 0b01111111;
    i2c_master_transmit(dev1, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_PULL_EN; wb[1] = 0b01111111;
    i2c_master_transmit(dev1, wb, 2, I2C_TIMEOUT_MS);
    // Drive EXT5V_EN(P2), TP_RST(P5), CAM_RST(P6) high; hold LCD_RST(P4) and SPK_EN(P1) low
    wb[0] = PI4IO_REG_OUT_SET; wb[1] = 0b01100100;
    i2c_master_transmit(dev1, wb, 2, I2C_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(10));
    // Release LCD_RST — ST7123 requires a clean low->high pulse before accepting commands
    wb[0] = PI4IO_REG_OUT_SET; wb[1] = 0b01110100;
    i2c_master_transmit(dev1, wb, 2, I2C_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(120));

    i2c_device_config_t dev_cfg2 = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PI4IOE2_ADDR,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_i2c_bus, &dev_cfg2, &dev2));
    s_pi4ioe2_dev = dev2;

    // PI4IOE2: P0=WLAN_PWR_EN, P3=USB5V_EN, P7=CHG_EN
    wb[0] = PI4IO_REG_CHIP_RESET; wb[1] = 0xFF;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_CHIP_RESET;
    i2c_master_transmit_receive(dev2, wb, 1, rb, 1, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_IO_DIR; wb[1] = 0b10111001;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_OUT_H_IM; wb[1] = 0b00000110;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_PULL_SEL; wb[1] = 0b10111001;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_PULL_EN; wb[1] = 0b11111001;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_IN_DEF_STA; wb[1] = 0b01000000;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
    wb[0] = PI4IO_REG_INT_MASK; wb[1] = 0b10111111;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
    // Drive WLAN_PWR_EN(P0), USB5V_EN(P3) high
    wb[0] = PI4IO_REG_OUT_SET; wb[1] = 0b00001001;
    i2c_master_transmit(dev2, wb, 2, I2C_TIMEOUT_MS);
}

void board_set_usb5v_en(bool enable)
{
    if (!s_pi4ioe2_dev) return;  // pi4ioe_init() hasn't run
    // WLAN_PWR_EN(P0) stays high either way; only USB5V_EN(P3) toggles. Must
    // be disabled before using the USB-A/OTG port as a USB *device* (e.g.
    // mass storage) -- otherwise this board's own 5V boost fights the host
    // PC's VBUS on the same line.
    uint8_t wb[2] = { PI4IO_REG_OUT_SET, (uint8_t)(enable ? 0b00001001 : 0b00000001) };
    i2c_master_transmit(s_pi4ioe2_dev, wb, 2, I2C_TIMEOUT_MS);
}

// --- RGB565 helpers ---

static inline uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

static inline void rgb565_to_rgb888(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = ((c >> 11) & 0x1F) << 3;
    *g = ((c >>  5) & 0x3F) << 2;
    *b = ((c >>  0) & 0x1F) << 3;
}

// --- board_interface.h implementation ---

void board_init(void)
{
    ESP_LOGI(TAG, "%s init", BOARD_NAME);

    // 1. I2C + IO expander (must happen before display init to assert LCD_RST)
    pi4ioe_init();

    // 2. LDO for MIPI DSI PHY
    esp_ldo_channel_handle_t phy_ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = {
        .chan_id    = DSI_PHY_LDO_CHAN,
        .voltage_mv = DSI_PHY_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo));

    // 3. Backlight PWM (LEDC, 12-bit, 5 kHz)
    const ledc_timer_config_t bl_timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LCD_LEDC_DUTY_RES,
        .timer_num       = LCD_LEDC_TIMER,
        .freq_hz         = LCD_LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&bl_timer));
    const ledc_channel_config_t bl_chan = {
        .gpio_num   = LCD_BACKLIGHT_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LCD_LEDC_CHAN,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LCD_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&bl_chan));

    // 4. MIPI DSI bus (2 lanes, 965 Mbps)
    esp_lcd_dsi_bus_handle_t dsi_bus = NULL;
    esp_lcd_dsi_bus_config_t dsi_cfg = {
        .bus_id             = 0,
        .num_data_lanes     = 2,
        .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DSI_LANE_BITRATE,
    };
    ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&dsi_cfg, &dsi_bus));

    // 5. DBI panel IO (command channel over DSI)
    esp_lcd_dbi_io_config_t dbi_cfg = {
        .virtual_channel = 0,
        .lcd_cmd_bits    = 8,
        .lcd_param_bits  = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_cfg, &s_panel_io));

    // 6. DPI panel (pixel data) — double-buffered so the map renderer can PPA-blit
    // into the back buffer while the front buffer is being scanned out (tear-free).
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel    = 0,
        .dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLOCK_MHZ,
        .in_color_format    = LCD_COLOR_FMT_RGB565,
        .num_fbs            = 2,
        .video_timing = {
            .h_size            = LCD_W,
            .v_size            = LCD_H,
            .hsync_pulse_width = 2,
            .hsync_back_porch  = 40,
            .hsync_front_porch = 40,
            .vsync_pulse_width = 2,
            .vsync_back_porch  = 8,
            .vsync_front_porch = 220,
        },
    };

    // 7. ST7123 vendor config
    st7123_vendor_config_t vendor_cfg = {
        .init_cmds      = s_st7123_init,
        .init_cmds_size = sizeof(s_st7123_init) / sizeof(s_st7123_init[0]),
        .mipi_config = {
            .dsi_bus    = dsi_bus,
            .dpi_config = &dpi_cfg,
            .lane_num   = 2,
        },
    };
    const esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = -1,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 24,
        .vendor_config  = &vendor_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7123(s_panel_io, &dev_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    // IDF 6.0: dpi_panel_config_t.flags.use_dma2d was replaced by this call
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_enable_dma2d(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

    // 7b. Refresh-done notification -- see dpi_refresh_done_cb()/board_lcd_commit().
    s_refresh_done_sem = xSemaphoreCreateBinary();
    assert(s_refresh_done_sem);
    esp_lcd_dpi_panel_event_callbacks_t refresh_cbs = { .on_refresh_done = dpi_refresh_done_cb };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &refresh_cbs, NULL));

    // 8. Get both hardware framebuffers and allocate software render buffer
    void *fb0 = NULL, *fb1 = NULL;
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_get_frame_buffer(s_panel, 2, &fb0, &fb1));
    s_fbs[0] = (uint8_t *)fb0;
    s_fbs[1] = (uint8_t *)fb1;
    s_back_idx = 0;

    s_backbuf = heap_caps_aligned_calloc(64, FB_SIZE, 1,
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    assert(s_backbuf);
    // Clear both hardware framebuffers to black
    memset(s_fbs[0], 0, FB_SIZE);
    esp_cache_msync(s_fbs[0], FB_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    memset(s_fbs[1], 0, FB_SIZE);
    esp_cache_msync(s_fbs[1], FB_SIZE, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    // 9. Backlight on (100%)
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN, LCD_LEDC_DUTY_MAX);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN);

    ESP_LOGI(TAG, "%s init done", BOARD_NAME);
}

void board_lcd_set_backlight(bool on)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN, on ? LCD_LEDC_DUTY_MAX : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN);
}

void board_lcd_set_brightness(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    // Same LEDC channel board_lcd_set_backlight() drives -- a screen-off
    // timeout still calls that for a full on/off, this just changes what
    // duty cycle "on" means. 12-bit resolution (LCD_LEDC_DUTY_MAX = 4095)
    // gives plenty of headroom for a 0-100 percent mapping.
    uint32_t duty = (uint32_t)percent * LCD_LEDC_DUTY_MAX / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LCD_LEDC_CHAN);
}

// See the comment on board_lcd_register_color_trans_done_cb() (board_interface.h)
// for why on_refresh_done and on_color_trans_done have to be (re-)registered
// together, in one call, rather than each owner registering its own
// independently. This forwards to whatever main/ui_shell.c registered, if
// anything -- NULL (the default, nothing registered yet) is a harmless no-op.
static esp_lcd_dpi_panel_color_trans_done_cb_t s_extra_color_trans_cb = NULL;
static void *s_extra_color_trans_ctx = NULL;

static bool dpi_color_trans_done_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx)
{
    if (s_extra_color_trans_cb) {
        return s_extra_color_trans_cb(panel, edata, s_extra_color_trans_ctx);
    }
    return false;
}

void board_lcd_register_color_trans_done_cb(esp_lcd_dpi_panel_color_trans_done_cb_t cb, void *user_ctx)
{
    s_extra_color_trans_cb = cb;
    s_extra_color_trans_ctx = user_ctx;
    esp_lcd_dpi_panel_event_callbacks_t cbs = {
        .on_color_trans_done = dpi_color_trans_done_cb,
        .on_refresh_done = dpi_refresh_done_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL));
}

const char *board_get_name(void) { return BOARD_NAME; }
bool        board_has_lcd(void)  { return s_panel != NULL; }

int board_lcd_width(void)  { return LCD_W; }
int board_lcd_height(void) { return LCD_H; }

void board_lcd_clear(void)
{
    if (s_backbuf) memset(s_backbuf, 0, FB_SIZE);
}

void board_lcd_flush(void)
{
    if (!s_backbuf || !s_fbs[s_back_idx]) return;
    memcpy(s_fbs[s_back_idx], s_backbuf, FB_SIZE);
    board_lcd_commit();
}

void board_lcd_flush_region(int x1, int y1, int x2, int y2)
{
    // Double-buffered now, but partial region flush still offers no bus
    // savings on this panel; delegate to full flush.
    (void)x1; (void)y1; (void)x2; (void)y2;
    board_lcd_flush();
}

void board_lcd_fill(uint16_t color)
{
    if (!s_backbuf) return;
    uint16_t *p = (uint16_t *)s_backbuf;
    for (int i = 0; i < LCD_W * LCD_H; i++) p[i] = color;
    board_lcd_flush();
}

void board_lcd_set_pixel_raw(int x, int y, uint16_t color)
{
    if (!s_backbuf || x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return;
    ((uint16_t *)s_backbuf)[y * LCD_W + x] = color;
}

void board_lcd_set_pixel_rgb(int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    board_lcd_set_pixel_raw(x, y, rgb888_to_rgb565(r, g, b));
}

uint16_t board_lcd_pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return rgb888_to_rgb565(r, g, b);
}

uint16_t board_lcd_get_pixel_raw(int x, int y)
{
    if (!s_backbuf || x < 0 || x >= LCD_W || y < 0 || y >= LCD_H) return 0;
    return ((uint16_t *)s_backbuf)[y * LCD_W + x];
}

void board_lcd_unpack_rgb(uint16_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    rgb565_to_rgb888(color, r, g, b);
}

uint8_t *board_lcd_hw_framebuffer(void) { return s_fbs[s_back_idx]; }

void board_lcd_commit(void)
{
    // Tell the DPI driver that the back buffer is ready — it detects the pointer
    // is already an internal framebuffer, skips the copy, syncs cache, and flips
    // cur_fb_index so the display scans this buffer from the next frame boundary.
    uint8_t *back = s_fbs[s_back_idx];
    if (!back || !s_panel) return;
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_W, LCD_H, back);
    s_back_idx ^= 1;

    // Block until this buffer has actually finished being scanned out before
    // letting the caller start compositing into the other one. Without this,
    // a render loop fast enough could occasionally race ahead of the panel's
    // true refresh rate (~57.8Hz, vs. the softer MAP_RENDER_FPS=60 target)
    // and start overwriting a buffer the DSI hardware was still actively
    // reading -- reproduced on real hardware as a localized stutter/flicker
    // in whichever screen region happened to be written earliest in the
    // composite pass and land latest in DSI scan order (the map's logical
    // top-left tiles, immediately next to the GPS status bar). This
    // naturally paces the whole render loop to the panel's real rate instead
    // of the softer target.
    if (s_refresh_done_sem) {
        xSemaphoreTake(s_refresh_done_sem, portMAX_DELAY);
    }
}

esp_lcd_panel_handle_t    board_lcd_panel_handle(void)    { return s_panel; }
esp_lcd_panel_io_handle_t board_lcd_panel_io_handle(void) { return s_panel_io; }
i2c_master_bus_handle_t   board_i2c_bus_handle(void)      { return s_i2c_bus; }

void board_lcd_sanity_test(void)
{
    if (!s_panel) return;
    ESP_LOGI(TAG, "Sanity test: cycling colors");
    static const struct { uint8_t r, g, b; } colors[] = {
        {255,   0,   0},
        {  0, 255,   0},
        {  0,   0, 255},
        {255, 255, 255},
        {  0,   0,   0},
    };
    for (int i = 0; i < 5; i++) {
        uint16_t px = rgb888_to_rgb565(colors[i].r, colors[i].g, colors[i].b);
        uint16_t *p = (uint16_t *)s_backbuf;
        for (int j = 0; j < LCD_W * LCD_H; j++) p[j] = px;
        board_lcd_flush();
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
