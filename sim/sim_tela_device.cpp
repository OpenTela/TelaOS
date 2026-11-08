// sim_tela_device.cpp — the "board" TelaOS runs on in the simulator.
//
// TelaOS picks its board at compile time in src/hal/device.cpp (Esp4848S040, Esp8048W550,
// TWatch2020). The simulator build leaves that file out and provides this one instead:
// flush and touch go to the virtual panel; everything above the HAL stays untouched.
#include "hal/device.h"
#include "sim_panel.h"
#include "sim_tela.h"

#include "Arduino.h"

#include <cstdio>

namespace {

class SimDevice final : public Device {
public:
    bool initPanel() override {
        std::printf("[sim] SimDevice: panel %dx%d\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        return true;
    }
    bool initTouch() override { return true; }
    void setBrightness(uint8_t level) override { brightness_ = level; }
    lv_display_flush_cb_t getFlushCallback() override { return &Sim::Panel::flushCb; }
    lv_indev_read_cb_t getTouchCallback() override { return &Sim::Panel::touchReadCb; }

    // Light sleep as on the T-Watch: the time goes on, the firmware's loop does not. No `override`:
    // a firmware from before Device::lightSleep() builds too, and never calls it
    bool lightSleep(uint32_t ms) {
        delay(ms);
        slept_ += ms;
        return true;
    }
    uint64_t slept() const { return slept_; }

private:
    uint8_t brightness_ = 255;
    uint64_t slept_ = 0;
};

SimDevice s_device;

}  // namespace

Device& Device::inst() { return s_device; }

uint64_t SimTela::sleptMs() { return s_device.slept(); }
