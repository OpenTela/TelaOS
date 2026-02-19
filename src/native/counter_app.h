#pragma once
/**
 * counter_app.h — Minimal test app
 */

#include "core/native_app.h"

class CounterApp : public NativeApp {
public:
    CounterApp() : NativeApp("counter", "Counter") {}

    int count = 0;

    Label lblTitle = {
        .text  = "Counter",
        .font  = 32,
        .color = 0x888888,
        .align = top_mid,
        .y     = 30
    };

    Label lblCount = {
        .text  = "0",
        .font  = 72,
        .color = 0xFFFFFF,
        .align = center,
        .y     = -30
    };

    Button btnPlus = {
        .text    = "+1",
        .bgcolor = 0x2E7D32,
        .align   = center,
        .y = 40, .w = 120, .h = 50,
        .onClick = [this] {
            count++;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", count);
            lblCount.setText(buf);
        },
    };

    Button btnReset = {
        .text    = "Reset",
        .bgcolor = 0x555555,
        .align   = center,
        .y = 100, .w = 120, .h = 50,
        .onClick = [this] {
            count = 0;
            lblCount.setText("0");
        },
    };

    Button btnBack = {
        .text    = "Home",
        .bgcolor = 0x333333,
        .align   = bottom_mid,
        .y = -20, .w = 140, .h = 45,
        .onClick = [this] { goHome(); },
    };

    void onCreate() override {
        ui::build(page(), 0x0D1117,
            lblTitle, lblCount, btnPlus, btnReset, btnBack
        );
    }
};

REGISTER_NATIVE_APP(CounterApp);
