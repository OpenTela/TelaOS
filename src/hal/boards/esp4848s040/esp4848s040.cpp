/**
 * Esp4848S040 — Hardware implementation
 *
 * Contains:
 *   - ST7701 3-Wire SPI init sequence
 *   - RGB panel configuration (esp_lcd)
 *   - GT911 touch init + coordinate mapping
 *   - Flush callback with R/B color fix (board-specific HW bug)
 *   - Touch callback
 */

 #ifdef BOARD_ESP4848S040

#include "esp4848s040.h"
#include <Arduino.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"

// ============================================
// Module-level state (accessed by C callbacks)
// ============================================
static esp_lcd_panel_handle_t s_panel = nullptr;
static TAMC_GT911 s_touch(PIN_TOUCH_SDA, PIN_TOUCH_SCL,
                           PIN_TOUCH_INT, PIN_TOUCH_RST,
                           SCREEN_WIDTH, SCREEN_HEIGHT);

// ============================================
// ST7701 3-Wire SPI (bit-bang)
// ============================================
static void spi_send_9bit(bool dc, uint8_t data) {
    digitalWrite(PIN_LCD_CS, LOW);

    digitalWrite(PIN_LCD_SCK, LOW);
    digitalWrite(PIN_LCD_SDA, dc ? HIGH : LOW);
    digitalWrite(PIN_LCD_SCK, HIGH);

    for (int i = 7; i >= 0; i--) {
        digitalWrite(PIN_LCD_SCK, LOW);
        digitalWrite(PIN_LCD_SDA, (data >> i) & 1);
        digitalWrite(PIN_LCD_SCK, HIGH);
    }

    digitalWrite(PIN_LCD_CS, HIGH);
}

static void lcd_cmd(uint8_t cmd)   { spi_send_9bit(false, cmd); }
static void lcd_data(uint8_t data) { spi_send_9bit(true, data);  }

// ============================================
// ST7701 Init — HSD4.0IPS (HSD040BPN1-A00)
// 480x480, 2dot inversion, 60Hz
// ============================================
static void st7701_init() {
    Serial.println("[ST7701] Init (HSD040BPN1-A00)...");

    pinMode(PIN_LCD_CS, OUTPUT);
    pinMode(PIN_LCD_SCK, OUTPUT);
    pinMode(PIN_LCD_SDA, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);
    digitalWrite(PIN_LCD_SCK, LOW);

    // Command2 BK3
    lcd_cmd(0xFF); lcd_data(0x77); lcd_data(0x01); lcd_data(0x00); lcd_data(0x00); lcd_data(0x13);
    lcd_cmd(0xEF); lcd_data(0x08);

    // Command2 BK0
    lcd_cmd(0xFF); lcd_data(0x77); lcd_data(0x01); lcd_data(0x00); lcd_data(0x00); lcd_data(0x10);

    lcd_cmd(0xC0); lcd_data(0x3B); lcd_data(0x00);
    lcd_cmd(0xC1); lcd_data(0x0D); lcd_data(0x02);
    lcd_cmd(0xC2); lcd_data(0x21); lcd_data(0x08);
    lcd_cmd(0xCD); lcd_data(0x00);

    // Positive Voltage Gamma
    lcd_cmd(0xB0);
    const uint8_t gp[] = {0x00,0x11,0x18,0x0E,0x11,0x06,0x07,0x08,0x07,0x22,0x04,0x12,0x0F,0xAA,0x31,0x18};
    for (int i = 0; i < 16; i++) lcd_data(gp[i]);

    // Negative Voltage Gamma
    lcd_cmd(0xB1);
    const uint8_t gn[] = {0x00,0x11,0x19,0x0E,0x12,0x07,0x08,0x08,0x08,0x22,0x04,0x11,0x11,0xA9,0x32,0x18};
    for (int i = 0; i < 16; i++) lcd_data(gn[i]);

    // Command2 BK1
    lcd_cmd(0xFF); lcd_data(0x77); lcd_data(0x01); lcd_data(0x00); lcd_data(0x00); lcd_data(0x11);

    lcd_cmd(0xB0); lcd_data(0x60);
    lcd_cmd(0xB1); lcd_data(0x30);
    lcd_cmd(0xB2); lcd_data(0x87);  // VCOM
    lcd_cmd(0xB3); lcd_data(0x80);
    lcd_cmd(0xB5); lcd_data(0x49);
    lcd_cmd(0xB7); lcd_data(0x85);
    lcd_cmd(0xB8); lcd_data(0x21);
    lcd_cmd(0xC1); lcd_data(0x78);
    lcd_cmd(0xC2); lcd_data(0x78);

    delay(20);

    lcd_cmd(0xE0); lcd_data(0x00); lcd_data(0x1B); lcd_data(0x02);

    lcd_cmd(0xE1);
    const uint8_t e1[] = {0x08,0xA0,0x00,0x00,0x07,0xA0,0x00,0x00,0x00,0x44,0x44};
    for (int i = 0; i < 11; i++) lcd_data(e1[i]);

    lcd_cmd(0xE2);
    const uint8_t e2[] = {0x11,0x11,0x44,0x44,0xED,0xA0,0x00,0x00,0xEC,0xA0,0x00,0x00};
    for (int i = 0; i < 12; i++) lcd_data(e2[i]);

    lcd_cmd(0xE3); lcd_data(0x00); lcd_data(0x00); lcd_data(0x11); lcd_data(0x11);
    lcd_cmd(0xE4); lcd_data(0x44); lcd_data(0x44);

    lcd_cmd(0xE5);
    const uint8_t e5[] = {0x0A,0xE9,0xD8,0xA0,0x0C,0xEB,0xD8,0xA0,0x0E,0xED,0xD8,0xA0,0x10,0xEF,0xD8,0xA0};
    for (int i = 0; i < 16; i++) lcd_data(e5[i]);

    lcd_cmd(0xE6); lcd_data(0x00); lcd_data(0x00); lcd_data(0x11); lcd_data(0x11);
    lcd_cmd(0xE7); lcd_data(0x44); lcd_data(0x44);

    lcd_cmd(0xE8);
    const uint8_t e8[] = {0x09,0xE8,0xD8,0xA0,0x0B,0xEA,0xD8,0xA0,0x0D,0xEC,0xD8,0xA0,0x0F,0xEE,0xD8,0xA0};
    for (int i = 0; i < 16; i++) lcd_data(e8[i]);

    lcd_cmd(0xEB);
    const uint8_t eb[] = {0x02,0x00,0xE4,0xE4,0x88,0x00,0x40};
    for (int i = 0; i < 7; i++) lcd_data(eb[i]);

    lcd_cmd(0xEC); lcd_data(0x3C); lcd_data(0x00);

    lcd_cmd(0xED);
    const uint8_t ed[] = {0xAB,0x89,0x76,0x54,0x02,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x20,0x45,0x67,0x98,0xBA};
    for (int i = 0; i < 16; i++) lcd_data(ed[i]);

    // Exit Command2
    lcd_cmd(0xFF); lcd_data(0x77); lcd_data(0x01); lcd_data(0x00); lcd_data(0x00); lcd_data(0x00);

    // RGB565
    lcd_cmd(0x3A); lcd_data(0x55);

    // MADCTL: RGB order
    lcd_cmd(0x36); lcd_data(0x00);

    // Sleep Out + Display On
    lcd_cmd(0x11);
    delay(120);
    lcd_cmd(0x29);
    delay(20);

    Serial.println("[ST7701] Done");
}

// ============================================
// RGB Panel (esp_lcd)
// ============================================
static void rgb_panel_init() {
    Serial.println("[RGB] Init...");

    gpio_reset_pin((gpio_num_t)STRAPPING_PIN_0);
    gpio_reset_pin((gpio_num_t)STRAPPING_PIN_1);

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
    cfg.data_width                     = DISPLAY_DATA_WIDTH;
    cfg.sram_trans_align               = DISPLAY_SRAM_ALIGN;
    cfg.psram_trans_align              = DISPLAY_PSRAM_ALIGN;

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

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    Serial.println("[RGB] Done");
}

// ============================================
// LVGL Flush — C callback (zero overhead)
//
// Board: ESP32-4848S040 (Jingcai)
// Panel: ST7701 RGB565, pin order B-G-R
//
// Probably HW issue: GPIO0 (R4/MSB) has 10K pull-up (R2)
// for boot mode strapping. During PSRAM DMA this
// resistor prevents clean bit toggling, corrupting
// the MSB of Red channel. Blue MSB (GPIO15) shows
// similar artifacts.
//
// Fix: shift R/B channels >>1, effectively using
// bits [3:0] and ignoring problematic MSB.
// Trade-off: 4-bit R/B (16 levels) instead of 5-bit (32).
//
// Proper fix: HZ. ESP-IDF 5.x bounce buffers?
//
// Current fix: compress R/B from 0..31 to 0..15 (>>1).
// ============================================
static void esp4848s040_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint16_t* px  = (uint16_t*)px_map;
    int32_t   len = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);

    for (int32_t i = 0; i < len; i++) {
        uint16_t c = px[i];
        uint16_t r = ((c >> 11) & 0x1F) >> 1;
        uint16_t g = (c >> 5) & 0x3F;
        uint16_t b = (c & 0x1F) >> 1;
        px[i] = (r << 11) | (g << 5) | b;
    }

    esp_lcd_panel_draw_bitmap(s_panel,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    lv_display_flush_ready(disp);
}

// ============================================
// LVGL Touch — C callback (zero overhead)
// ============================================
static void esp4848s040_touch(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
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
bool Esp4848S040::initPanel() {
    st7701_init();
    rgb_panel_init();
    return true;
}

bool Esp4848S040::initTouch() {
    Serial.println("[Touch] Init GT911...");
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
    s_touch.begin();
    s_touch.setRotation(ROTATION_NORMAL);
    Serial.println("[Touch] OK");
    return true;
}

void Esp4848S040::setBrightness(uint8_t level) {
    static bool initialized = false;
    if (!initialized) {
        ledcSetup(0, 5000, 8);           // channel 0, 5kHz, 8-bit
        ledcAttachPin(PIN_BACKLIGHT, 0);  // attach pin to channel 0
        initialized = true;
    }
    ledcWrite(0, level);
}

lv_display_flush_cb_t Esp4848S040::getFlushCallback() {
    return esp4848s040_flush;
}

lv_indev_read_cb_t Esp4848S040::getTouchCallback() {
    return esp4848s040_touch;
}

#endif