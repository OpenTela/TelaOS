#ifdef BOARD_ESP8048W550
/**
 * Esp8048W550 — Hardware implementation
 *
 * Uses esp_lcd_panel_rgb (ESP-IDF native) for ST7262 RGB panel.
 * ST7262 is a "dumb" RGB controller — no SPI init sequence needed.
 *
 * Key differences vs ST7701 (esp4848s040):
 * - Needs bounce buffer for reliable PSRAM DMA
 * - Needs de_idle_high=1, pclk_idle_high=1
 * - No SPI init sequence
 */

#include "esp8048w550.h"
#include <Arduino.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"

// ============================================
// Module-level state
// ============================================
static esp_lcd_panel_handle_t s_panel = nullptr;

static TAMC_GT911 s_touch(PIN_TOUCH_SDA, PIN_TOUCH_SCL,
                           PIN_TOUCH_INT, PIN_TOUCH_RST,
                           SCREEN_WIDTH, SCREEN_HEIGHT);

// ============================================
// RGB Panel init for ST7262
// ============================================
static void rgb_panel_init() {
    Serial.println("[RGB] Init ST7262 800x480...");

    // Release strapping pins used as RGB data lines
    gpio_reset_pin(GPIO_NUM_3);   // B1
    gpio_reset_pin(GPIO_NUM_45);  // R0
    gpio_reset_pin(GPIO_NUM_46);  // B2

    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.clk_src                        = LCD_CLK_SRC_PLL160M;
    cfg.timings.pclk_hz                = DISPLAY_PCLK_HZ;
    cfg.timings.h_res                  = SCREEN_WIDTH;
    cfg.timings.v_res                  = SCREEN_HEIGHT;
    cfg.timings.hsync_front_porch      = DISPLAY_HSYNC_FRONT;
    cfg.timings.hsync_pulse_width      = DISPLAY_HSYNC_PULSE;
    cfg.timings.hsync_back_porch       = DISPLAY_HSYNC_BACK;
    cfg.timings.vsync_front_porch      = DISPLAY_VSYNC_FRONT;
    cfg.timings.vsync_pulse_width      = DISPLAY_VSYNC_PULSE;
    cfg.timings.vsync_back_porch       = DISPLAY_VSYNC_BACK;
    cfg.timings.flags.pclk_active_neg  = DISPLAY_PCLK_ACTIVE_NEG;
    cfg.timings.flags.de_idle_high     = 1;  // ST7262 needs this!
    cfg.timings.flags.pclk_idle_high   = 1;  // ST7262 needs this!
    cfg.data_width                     = DISPLAY_DATA_WIDTH;
    cfg.sram_trans_align               = DISPLAY_SRAM_ALIGN;
    cfg.psram_trans_align              = DISPLAY_PSRAM_ALIGN;
    cfg.num_fbs                        = 1;

    cfg.hsync_gpio_num  = PIN_HSYNC;
    cfg.vsync_gpio_num  = PIN_VSYNC;
    cfg.de_gpio_num     = PIN_DE;
    cfg.pclk_gpio_num   = PIN_PCLK;
    cfg.disp_gpio_num   = -1;

    const int data_pins[] = { DISPLAY_DATA_GPIO_NUMS };
    for (int i = 0; i < DISPLAY_DATA_WIDTH; i++) {
        cfg.data_gpio_nums[i] = data_pins[i];
    }

    cfg.flags.fb_in_psram = DISPLAY_FB_IN_PSRAM;

    // Bounce buffer: DMA reads from internal SRAM, CPU copies from PSRAM
    // Without this, DMA from PSRAM is unreliable for ST7262 RGB panels
    cfg.bounce_buffer_size_px = SCREEN_WIDTH * 10;  // 10 lines

    Serial.printf("[RGB] PCLK: %d Hz, bounce: %d px\n",
                  DISPLAY_PCLK_HZ, cfg.bounce_buffer_size_px);

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    Serial.println("[RGB] Panel init OK");
}

// ============================================
// LVGL Flush
// ============================================
static void esp8048w550_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    esp_lcd_panel_draw_bitmap(s_panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    lv_display_flush_ready(disp);
}

// ============================================
// LVGL Touch
// ============================================
static void esp8048w550_touch(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    s_touch.read();

    if (s_touch.isTouched) {
        int16_t tx = s_touch.points[0].x;
        int16_t ty = s_touch.points[0].y;

        #if TOUCH_XY_SWAP
        int16_t tmp = tx; tx = ty; ty = tmp;
        #endif

        #if TOUCH_X_INVERT
        data->point.x = map(tx, SCREEN_WIDTH, 0, 0, SCREEN_WIDTH - 1);
        #else
        data->point.x = tx;
        #endif

        #if TOUCH_Y_INVERT
        data->point.y = map(ty, SCREEN_HEIGHT, 0, 0, SCREEN_HEIGHT - 1);
        #else
        data->point.y = ty;
        #endif

        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ============================================
// Device interface
// ============================================
bool Esp8048W550::initPanel() {
    rgb_panel_init();
    return true;
}

bool Esp8048W550::initTouch() {
    Serial.println("[Touch] Init GT911...");
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
    s_touch.begin();
    s_touch.setRotation(ROTATION_NORMAL);
    Serial.println("[Touch] OK");
    return true;
}

void Esp8048W550::setBrightness(uint8_t level) {
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, level > 0 ? HIGH : LOW);
}

lv_display_flush_cb_t Esp8048W550::getFlushCallback() {
    return esp8048w550_flush;
}

lv_indev_read_cb_t Esp8048W550::getTouchCallback() {
    return esp8048w550_touch;
}

#endif // BOARD_ESP8048W550