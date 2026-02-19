#pragma once
/**
 * Esp4848S040 — Device implementation
 *
 * ESP32-S3 + ST7701 (RGB panel) + GT911 (capacitive touch)
 * 4.0" IPS 480x480
 */

#include "hal/boards/esp4848s040/esp4848s040_config.h"
#include "hal/device.h"

class Esp4848S040 : public Device {
public:
    bool initPanel() override;
    bool initTouch() override;
    void setBrightness(uint8_t level) override;

    lv_display_flush_cb_t  getFlushCallback() override;
    lv_indev_read_cb_t     getTouchCallback() override;
};
