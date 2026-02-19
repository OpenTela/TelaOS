#pragma once
/**
 * Board Configuration — TTGO T-Watch 2020 (V1/V3)
 *
 * Hardware:
 *   - MCU: ESP32-D0WDQ6-V3 (dual-core LX6, 520KB SRAM, 8MB PSRAM, 16MB Flash)
 *   - Display: 1.54" IPS 240x240, ST7789V controller, SPI interface
 *   - Touch: FT6336 capacitive (I2C, separate bus)
 *   - PMU: AXP202 (battery, backlight power, shutdown)
 *   - RTC: PCF8563
 *   - Accel: BMA423 (step counter, wrist tilt)
 *   - Audio: MAX98357 I2S + speaker (V1), + PDM mic (V3)
 *   - Vibration motor, IR transmitter
 */

// ============================================
// Display SPI — ST7789V
// ============================================
#define PIN_LCD_MOSI            19
#define PIN_LCD_SCLK            18
#define PIN_LCD_CS               5
#define PIN_LCD_DC              27
#define PIN_LCD_RST             -1      // No HW reset — power-cycled via AXP202

#define DISPLAY_SPI_HOST        SPI2_HOST
#define DISPLAY_SPI_FREQ_HZ     40000000    // 40 MHz

#define DISPLAY_INVERT_COLOR    1

#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0

#define TOUCH_CS                -1      // No SPI touch. Use I2C driver instead

// ============================================
// Backlight
// ============================================
#define PIN_BACKLIGHT           15

// ============================================
// Touch — FT6336 (separate I2C bus!)
// ============================================
#define PIN_TOUCH_SDA           23
#define PIN_TOUCH_SCL           32
#define PIN_TOUCH_INT           38
#define FT6336_I2C_ADDR         0x38
#define FT6336_I2C_NUM          I2C_NUM_1
#define FT6336_I2C_FREQ         400000

#define TOUCH_X_INVERT          0
#define TOUCH_Y_INVERT          0
#define TOUCH_XY_SWAP           0
#define TOUCH_X_OFFSET          -10
#define TOUCH_Y_OFFSET          -5

// ============================================
// Main I2C bus (bus 0)
// ============================================
#define PIN_I2C_SDA             21
#define PIN_I2C_SCL             22

// AXP202 PMU
#define AXP202_I2C_ADDR         0x35
#define AXP202_INT_PIN          35

// PCF8563 RTC
#define PCF8563_I2C_ADDR        0x51
#define PCF8563_INT_PIN         37

// BMA423 Accelerometer
#define BMA423_I2C_ADDR         0x19

// ============================================
// AXP202 registers
// ============================================
#define AXP202_REG_POWER_CTL    0x12
#define AXP202_REG_LDO24_VOLT   0x28
#define AXP202_REG_ADC_EN1      0x82
#define AXP202_REG_VBAT_H       0x78
#define AXP202_REG_VBAT_L       0x79

#define AXP202_LDO2_BIT         (1 << 2)
#define AXP202_LDO3_BIT         (1 << 3)
#define AXP202_LDO2_VOLTAGE     0x0F    // 3.3V

// ============================================
// Audio — I2S
// ============================================
#define PIN_I2S_BCK             26
#define PIN_I2S_WS              25
#define PIN_I2S_DOUT            33

// ============================================
// Misc
// ============================================
#define PIN_VIBRO_MOTOR          4
#define PIN_IR_SEND             13