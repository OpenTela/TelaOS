/**
 * font.h - Centralized font management for UI engine
 */

#pragma once

#include "lvgl.h"

// External font declarations (defined in font/*.c files)
extern "C" {
    extern const lv_font_t Ubuntu_16px;
    extern const lv_font_t Ubuntu_32px;
    extern const lv_font_t Ubuntu_48px;
    extern const lv_font_t Ubuntu_72px;
}

namespace UI {

class Font {
public:
    static constexpr int SMALL = 16;
    static constexpr int MEDIUM = 32;
    static constexpr int LARGE = 48;
    static constexpr int XLARGE = 72;
    
    static int nearest(int requested);
    static const lv_font_t* get(int size = SMALL);
    static const lv_font_t* defaultFont();

private:
    Font() = delete;
};

} // namespace UI

// Legacy access
namespace Font {
    inline const lv_font_t* get(int size) { return UI::Font::get(size); }
}
