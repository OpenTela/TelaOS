// sim_panel.h — virtual display panel of the watch, with its touch controller.
//
// The firmware's display HAL creates the LVGL display and takes flushCb / touchReadCb from
// its Device (sim_tela_device.cpp). The panel keeps what was flushed:
//  - RGB565 GRAM with the final image: screenshots and the SDL window read it
//  - single-point touch controller
#pragma once

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Sim {

struct PanelConfig {
    int width = 480;  // esp4848s040
    int height = 480;
};

class Panel {
public:
    void init(const PanelConfig& cfg);

    static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data);

    int width() const { return cfg_.width; }
    int height() const { return cfg_.height; }
    const uint16_t* gram() const { return gram_.data(); }

    // Touch controller state. A press shorter than one LVGL read period is latched,
    // so fast mouse clicks are never lost.
    void touch(int x, int y, bool pressed);
    bool touchPressed() const { return touch_pressed_; }
    int touchX() const { return touch_x_; }
    int touchY() const { return touch_y_; }

    uint32_t frames() const { return frames_; }    // completed refreshes
    uint32_t flushes() const { return flushes_; }  // flush calls (partial areas)
    bool takeDirty();                              // GRAM changed since last call?

    bool savePng(const std::string& path) const;

private:
    static Panel* s_active;  // one panel per process, like one screen per watch

    PanelConfig cfg_;
    std::vector<uint16_t> gram_;
    int touch_x_ = 0;
    int touch_y_ = 0;
    bool touch_pressed_ = false;
    bool press_latched_ = false;
    uint32_t frames_ = 0;
    uint32_t flushes_ = 0;
    bool dirty_ = true;
};

}  // namespace Sim
