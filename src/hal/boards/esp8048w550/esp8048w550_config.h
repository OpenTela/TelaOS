#pragma once
/**
 * Board Configuration — ESP8048W550
 *
 * Hardware:
 *   - MCU: ESP32-S3-WROOM-1 (dual-core, 8MB PSRAM, 16MB Flash)
 *   - Display: 5.0" IPS 800x480, ST7262 controller, RGB interface
 *   - Touch: GT911 capacitive
 *   - Audio: I2S DAC
 *   - SD: SPI
 *   - Battery: JST connector + switch
 */

// ============================================
// Pixel clock
// ============================================
#define DISPLAY_PCLK_HZ        12000000    // 12 MHz (from vendor example)
#define DISPLAY_DATA_WIDTH      16          // RGB565 bus

// ============================================
// RGB timings (from vendor Arduino_RPi_DPI_RGBPanel config)
// ============================================
#define DISPLAY_HSYNC_FRONT     8
#define DISPLAY_HSYNC_PULSE     4
#define DISPLAY_HSYNC_BACK      8
#define DISPLAY_VSYNC_FRONT     8
#define DISPLAY_VSYNC_PULSE     4
#define DISPLAY_VSYNC_BACK      8
#define DISPLAY_PCLK_ACTIVE_NEG 1           // inverted vs ESP4848

// DMA alignment
#define DISPLAY_SRAM_ALIGN      8
#define DISPLAY_PSRAM_ALIGN     64

// Framebuffer location: 1 = PSRAM, 0 = SRAM
#define DISPLAY_FB_IN_PSRAM     1

// ============================================
// Backlight
// ============================================
#define PIN_BACKLIGHT           2

// ============================================
// RGB interface pins (from vendor .ino)
// ============================================
#define PIN_DE                  40
#define PIN_VSYNC               41
#define PIN_HSYNC               39
#define PIN_PCLK                42

// Red (5 bits)
#define PIN_R0                  45
#define PIN_R1                  48
#define PIN_R2                  47
#define PIN_R3                  21
#define PIN_R4                  14

// Green (6 bits)
#define PIN_G0                  5
#define PIN_G1                  6
#define PIN_G2                  7
#define PIN_G3                  15
#define PIN_G4                  16
#define PIN_G5                  4

// Blue (5 bits)
#define PIN_B0                  8
#define PIN_B1                  3
#define PIN_B2                  46
#define PIN_B3                  9
#define PIN_B4                  1

// Data pin mapping for esp_lcd_rgb_panel (B-G-R order on bus)
#define DISPLAY_DATA_GPIO_NUMS \
    PIN_B0, PIN_B1, PIN_B2, PIN_B3, PIN_B4, \
    PIN_G0, PIN_G1, PIN_G2, PIN_G3, PIN_G4, PIN_G5, \
    PIN_R0, PIN_R1, PIN_R2, PIN_R3, PIN_R4

// ============================================
// Touch (GT911)
// ============================================
#define PIN_TOUCH_SDA           19
#define PIN_TOUCH_SCL           20
#define PIN_TOUCH_INT           -1
#define PIN_TOUCH_RST           38

// Coordinate mapping (raw -> screen)
// Vendor example maps 800->0, 480->0
#define TOUCH_X_INVERT          1
#define TOUCH_Y_INVERT          1
#define TOUCH_XY_SWAP           0

// ============================================
// SD Card (SPI) — for future use
// ============================================
#define PIN_SD_CS               10
#define PIN_SD_MOSI             11
#define PIN_SD_MISO             13
#define PIN_SD_SCK              12

// ============================================
// I2S Audio — for future use
// ============================================
#define PIN_I2S_BCLK            0
#define PIN_I2S_LRC             18
#define PIN_I2S_DOUT            17