// #pragma once
/**
 * Esp8048W550 — Device implementation
 *
 * ESP32-S3 + ST7262 (RGB panel, no SPI init) + GT911 (capacitive touch)
 * 5.0" IPS 800x480
 */

#include "hal/boards/esp8048w550/esp8048w550_config.h"
#include "hal/device.h"

class Esp8048W550 : public Device {
public:
    bool initPanel() override;
    bool initTouch() override;
    void setBrightness(uint8_t level) override;

    lv_display_flush_cb_t  getFlushCallback() override;
    lv_indev_read_cb_t     getTouchCallback() override;
};