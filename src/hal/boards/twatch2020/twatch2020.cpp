#ifdef BOARD_TWATCH_2020
/**
 * TWatch2020 — Hardware implementation
 *
 * Uses TFT_eSPI (proven library for T-Watch)
 *
 * I2C topology:
 *   Bus 0 (Wire):  SDA=21 SCL=22 → AXP202, PCF8563, BMA423
 *   Bus 1 (Wire1): SDA=23 SCL=32 → FT6336 touch
 */

#include "twatch2020.h"
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>

// ============================================
// Module-level state
// ============================================
static TFT_eSPI* s_tft = nullptr;

// ============================================
// AXP202 — Minimal I2C driver
// ============================================
namespace AXP {

static uint8_t readReg(uint8_t reg) {
    Wire.beginTransmission(AXP202_I2C_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)AXP202_I2C_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

static void writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AXP202_I2C_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static bool init() {
    Wire.beginTransmission(AXP202_I2C_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[AXP202] ERROR: not found!");
        return false;
    }
    Serial.println("[AXP202] Found");

    uint8_t adc = readReg(AXP202_REG_ADC_EN1);
    adc |= 0x80;
    writeReg(AXP202_REG_ADC_EN1, adc);
    return true;
}

static void setLDO2(bool on) {
    uint8_t val = readReg(AXP202_REG_POWER_CTL);
    if (on) val |= AXP202_LDO2_BIT;
    else    val &= ~AXP202_LDO2_BIT;
    writeReg(AXP202_REG_POWER_CTL, val);
}

static void setLDO2Voltage(uint8_t level4bit) {
    uint8_t val = readReg(AXP202_REG_LDO24_VOLT);
    val = (val & 0x0F) | ((level4bit & 0x0F) << 4);
    writeReg(AXP202_REG_LDO24_VOLT, val);
}

static float getBatteryVoltage() {
    uint16_t hi = readReg(AXP202_REG_VBAT_H);
    uint16_t lo = readReg(AXP202_REG_VBAT_L) & 0x0F;
    return ((hi << 4) | lo) * 1.1f / 1000.0f;
}

} // namespace AXP

// ============================================
// FT6336 — Minimal I2C touch
// ============================================
namespace FT6336 {

static bool s_initialized = false;

static bool init() {
    Wire1.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, FT6336_I2C_FREQ);

    Wire1.beginTransmission(FT6336_I2C_ADDR);
    if (Wire1.endTransmission() != 0) {
        Serial.println("[FT6336] ERROR: not found!");
        return false;
    }
    Serial.println("[FT6336] Found");

    if (PIN_TOUCH_INT >= 0) {
        pinMode(PIN_TOUCH_INT, INPUT);
    }

    s_initialized = true;
    return true;
}

static bool read(uint16_t* x, uint16_t* y) {
    if (!s_initialized) return false;

    Wire1.beginTransmission(FT6336_I2C_ADDR);
    Wire1.write(0x02);
    if (Wire1.endTransmission(false) != 0) return false;

    Wire1.requestFrom((uint8_t)FT6336_I2C_ADDR, (uint8_t)5);
    if (Wire1.available() < 5) return false;

    uint8_t touchCount = Wire1.read() & 0x0F;
    uint8_t xh = Wire1.read() & 0x0F;
    uint8_t xl = Wire1.read();
    uint8_t yh = Wire1.read() & 0x0F;
    uint8_t yl = Wire1.read();

    // FT6336 supports max 2 touch points
    if (touchCount == 0 || touchCount > 2) return false;

    *x = (xh << 8) | xl;
    *y = (yh << 8) | yl;
    
    // Sanity check
    if (*x >= SCREEN_WIDTH || *y >= SCREEN_HEIGHT) return false;

    return true;
}

} // namespace FT6336

// ============================================
// LVGL Flush — via TFT_eSPI
// ============================================
static void twatch2020_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    s_tft->startWrite();
    s_tft->setAddrWindow(area->x1, area->y1, w, h);
    s_tft->pushColors((uint16_t*)px_map, w * h, true);
    s_tft->endWrite();

    lv_display_flush_ready(disp);
}

// ============================================
// LVGL Touch
// ============================================
static void twatch2020_touch(lv_indev_t*, lv_indev_data_t* data) {
    uint16_t tx, ty;
    
    if (FT6336::read(&tx, &ty)) {
        #if TOUCH_XY_SWAP
        uint16_t tmp = tx; tx = ty; ty = tmp;
        #endif

        #if TOUCH_X_INVERT
        data->point.x = SCREEN_WIDTH - 1 - tx;
        #else
        data->point.x = tx;
        #endif

        #if TOUCH_Y_INVERT
        data->point.y = SCREEN_HEIGHT - 1 - ty;
        #else
        data->point.y = ty;
        #endif

        data->point.x += TOUCH_X_OFFSET;
        data->point.y += TOUCH_Y_OFFSET;

        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ============================================
// Device interface
// ============================================
bool TWatch2020::initPanel() {
    Serial.println("[TWatch2020] Init...");

    // I2C for AXP202
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!AXP::init()) {
        Serial.println("[TWatch2020] WARNING: AXP202 failed");
    }

    // Power on display via LDO2
    AXP::setLDO2Voltage(AXP202_LDO2_VOLTAGE);
    AXP::setLDO2(true);
    delay(20);

    // Backlight ON (GPIO15 for V3)
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, HIGH);
    Serial.println("[Backlight] GPIO15 ON");

    // TFT init
    Serial.println("[ST7789] Init TFT_eSPI...");
    s_tft = new TFT_eSPI(SCREEN_WIDTH, SCREEN_HEIGHT);
    s_tft->init();
    s_tft->setRotation(2);  // 180° - button top-right
    s_tft->fillScreen(TFT_BLACK);
    Serial.println("[ST7789] Done");

    float vbat = AXP::getBatteryVoltage();
    Serial.printf("[TWatch2020] Battery: %.2fV\n", vbat);
    Serial.println("[TWatch2020] Ready");
    return true;
}

bool TWatch2020::initTouch() {
    Serial.println("[Touch] Init FT6336...");
    if (!FT6336::init()) return false;
    Serial.println("[Touch] OK");
    return true;
}

void TWatch2020::setBrightness(uint8_t level) {
    AXP::setLDO2(level > 0);
    pinMode(PIN_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_BACKLIGHT, level > 0 ? HIGH : LOW);
}

lv_display_flush_cb_t TWatch2020::getFlushCallback() {
    return twatch2020_flush;
}

lv_indev_read_cb_t TWatch2020::getTouchCallback() {
    return twatch2020_touch;
}

#endif // BOARD_TWATCH_2020