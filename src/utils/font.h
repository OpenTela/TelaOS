/**
 * font.h - Centralized font management for UI engine
 */

#pragma once

#include "lvgl.h"

// External font declarations (defined in font/*.c files). Not const, as they are defined there:
// UI::Font sets their fallback, and a write to an object declared const may be dropped
extern "C" {
    extern lv_font_t Ubuntu_16px;
    extern lv_font_t Ubuntu_32px;
    extern lv_font_t Ubuntu_48px;
    extern lv_font_t Ubuntu_72px;
    // The other languages of the same face, the fallback of the font of the same size
    // (scripts/make_fonts.py)
    extern const lv_font_t UbuntuExt_16px;
    extern const lv_font_t UbuntuExt_32px;
    extern const lv_font_t UbuntuExt_48px;
    extern const lv_font_t UbuntuExt_72px;
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
    // The size of a font get() gives (16, 32, 48, 72); SMALL for any other font or none
    static int sizeOf(const lv_font_t* font);

private:
    Font() = delete;
};

} // namespace UI

// Legacy access
namespace Font {
    inline const lv_font_t* get(int size) { return UI::Font::get(size); }
}
