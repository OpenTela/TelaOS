#pragma once
/**
 * TWatch2020 — Device implementation
 *
 * ESP32 + ST7789V (SPI) + FT6336 (I2C touch) + AXP202 (PMU)
 * 1.54" IPS 240x240, wristwatch form factor
 */

#include "twatch2020_config.h"
#include "hal/device.h"

class TWatch2020 : public Device {
public:
    bool initPanel() override;
    bool initTouch() override;
    void setBrightness(uint8_t level) override;
    bool lightSleep(uint32_t ms) override;

    lv_display_flush_cb_t  getFlushCallback() override;
    lv_indev_read_cb_t     getTouchCallback() override;
};