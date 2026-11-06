/**
 * font.h - Centralized font management for UI engine
 */

#pragma once

#include "lvgl.h"

#include <cstdint>
#include <string_view>

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
    // The font of a size (the next one not smaller of the four). bold: the bold face, a file on
    // LittleFS loaded the first time it is asked for; without the file, the regular one.
    static const lv_font_t* get(int size = SMALL, bool bold = false);
    static const lv_font_t* defaultFont();
    // The size of a font get() gives (16, 32, 48, 72), bold or not; SMALL for any other font or none
    static int sizeOf(const lv_font_t* font);
    // Whether a font is a bold face get() gives
    static bool isBold(const lv_font_t* font);

    // The fonts kept as files on LittleFS, data/system/fonts/ (scripts/make_fonts.py): the icon
    // font of each size goes between Ubuntu_*px and UbuntuExt_*px, its names into iconCode().
    // Once, after LittleFS is mounted; a file that is not there leaves its glyphs out. The bold
    // faces wait until get() asks for one.
    static void loadFiles();
    // The code of an icon by its Material Symbols name ("home"); 0 if there is no such icon
    static uint32_t iconCode(std::string_view name);

private:
    Font() = delete;
};

} // namespace UI

// Legacy access
namespace Font {
    inline const lv_font_t* get(int size, bool bold = false) { return UI::Font::get(size, bold); }
}
