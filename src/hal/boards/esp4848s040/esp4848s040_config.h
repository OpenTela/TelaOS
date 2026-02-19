#pragma once
/**
 * Board Configuration — ESP32-4848S040
 *
 * Compile-time constants for this specific board.
 * Included globally via: -include hal/boards/esp4848s040/board_config.h
 *
 * Hardware:
 *   - MCU: ESP32-S3 (dual-core, 8MB PSRAM octal)
 *   - Display: 4.0" IPS 480x480, ST7701 controller, RGB interface
 *   - Touch: GT911 capacitive
 *   - Flash: 16MB
 */

// ============================================
// Pixel clock
// ============================================
// Max depends on PSRAM config:
//   Quad PSRAM 80MHz  -> max ~11 MHz
//   Octal PSRAM 80MHz -> max ~22 MHz
#define DISPLAY_PCLK_HZ        16000000
#define DISPLAY_DATA_WIDTH      16          // RGB565 bus

// ============================================
// RGB timings
// ============================================
#define DISPLAY_HSYNC_FRONT     10
#define DISPLAY_HSYNC_PULSE     8
#define DISPLAY_HSYNC_BACK      50
#define DISPLAY_VSYNC_FRONT     10
#define DISPLAY_VSYNC_PULSE     8
#define DISPLAY_VSYNC_BACK      20
#define DISPLAY_PCLK_ACTIVE_NEG 0

// DMA alignment
#define DISPLAY_SRAM_ALIGN      8
#define DISPLAY_PSRAM_ALIGN     64

// Framebuffer location: 1 = PSRAM, 0 = SRAM
#define DISPLAY_FB_IN_PSRAM     1

// ============================================
// Panel controller pins (ST7701 3-Wire SPI)
// ============================================
#define PIN_LCD_CS              39
#define PIN_LCD_SCK             48
#define PIN_LCD_SDA             47

// Backlight
#define PIN_BACKLIGHT           38

// ============================================
// RGB interface pins
// ============================================
#define PIN_HSYNC               16
#define PIN_VSYNC               17
#define PIN_DE                  18
#define PIN_PCLK                21

// Red (5 bits)
#define PIN_R0                  11
#define PIN_R1                  12
#define PIN_R2                  13
#define PIN_R3                  14
#define PIN_R4                  0

// Green (6 bits)
#define PIN_G0                  8
#define PIN_G1                  20
#define PIN_G2                  3
#define PIN_G3                  46
#define PIN_G4                  9
#define PIN_G5                  10

// Blue (5 bits)
#define PIN_B0                  4
#define PIN_B1                  5
#define PIN_B2                  6
#define PIN_B3                  7
#define PIN_B4                  15

// Data pin mapping for esp_lcd_rgb_panel (B-G-R order on bus)
#define DISPLAY_DATA_GPIO_NUMS \
    PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4, \
    PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5, \
    PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4

// ============================================
// Touch (GT911)
// ============================================
#define PIN_TOUCH_SDA           19
#define PIN_TOUCH_SCL           45
#define PIN_TOUCH_INT           -1
#define PIN_TOUCH_RST           -1

// Coordinate mapping (raw -> screen)
#define TOUCH_X_INVERT          1
#define TOUCH_Y_INVERT          1
#define TOUCH_XY_SWAP           0

// Strapping pins (reset after boot)
#define STRAPPING_PIN_0         0
#define STRAPPING_PIN_1         15
