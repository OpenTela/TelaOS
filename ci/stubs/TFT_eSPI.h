#pragma once
#include <cstdint>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

class TFT_eSPI {
public:
    TFT_eSPI() = default;
    TFT_eSPI(int w, int h) {}
    void init() {}
    void setRotation(int) {}
    void fillScreen(uint32_t) {}
    void drawPixel(int, int, uint16_t) {}
    void startWrite() {}
    void endWrite() {}
    void pushColors(uint16_t*, uint32_t, bool = true) {}
    void setAddrWindow(int, int, int, int) {}
    int width() { return 480; }
    int height() { return 480; }
};
