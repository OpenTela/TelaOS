// sim_panel.cpp — virtual display panel: GRAM + touch.
#include "sim_panel.h"

#include "sim_png.h"

#include <algorithm>
#include <cstring>

namespace Sim {

Panel* Panel::s_active = nullptr;

void Panel::init(const PanelConfig& cfg) {
    cfg_ = cfg;
    gram_.assign(static_cast<size_t>(cfg.width) * cfg.height, 0);
    s_active = this;
}

void Panel::flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    Panel* self = s_active;
    const int w = lv_area_get_width(area);
    const uint32_t src_stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    for (int y = area->y1; y <= area->y2; ++y) {
        std::memcpy(&self->gram_[static_cast<size_t>(y) * self->cfg_.width + area->x1], px_map,
                    static_cast<size_t>(w) * 2);
        px_map += src_stride;
    }
    self->flushes_++;
    self->dirty_ = true;
    if (lv_display_flush_is_last(disp)) self->frames_++;
    lv_display_flush_ready(disp);
}

void Panel::touch(int x, int y, bool pressed) {
    touch_x_ = std::clamp(x, 0, cfg_.width - 1);
    touch_y_ = std::clamp(y, 0, cfg_.height - 1);
    if (pressed && !touch_pressed_) press_latched_ = true;
    touch_pressed_ = pressed;
}

void Panel::touchReadCb(lv_indev_t* /*indev*/, lv_indev_data_t* data) {
    Panel* self = s_active;
    data->point.x = self->touch_x_;
    data->point.y = self->touch_y_;
    data->state = (self->touch_pressed_ || self->press_latched_) ? LV_INDEV_STATE_PRESSED
                                                                 : LV_INDEV_STATE_RELEASED;
    self->press_latched_ = false;
}

bool Panel::takeDirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
}

bool Panel::savePng(const std::string& path) const {
    std::vector<uint8_t> rgb(gram_.size() * 3);
    for (size_t i = 0; i < gram_.size(); ++i) {
        const uint16_t c = gram_[i];
        const uint8_t r5 = c >> 11, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
        rgb[i * 3 + 0] = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        rgb[i * 3 + 1] = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        rgb[i * 3 + 2] = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
    }
    return writePng(path, cfg_.width, cfg_.height, rgb.data());
}

}  // namespace Sim
