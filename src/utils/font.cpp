#include "utils/font.h"

namespace UI {

#if !defined(LVGL_MOCK_ENABLED)
namespace {

// A glyph Ubuntu_*px does not have (ä, Ł, Ω, Ґ, №, ≤…) is drawn from UbuntuExt_*px of the
// same size. Set before main(): the launcher takes &Ubuntu_*px directly, not through get().
const bool s_fallbacks = [] {
    Ubuntu_16px.fallback = &UbuntuExt_16px;
    Ubuntu_32px.fallback = &UbuntuExt_32px;
    Ubuntu_48px.fallback = &UbuntuExt_48px;
    Ubuntu_72px.fallback = &UbuntuExt_72px;
    return true;
}();

}  // namespace
#endif

int Font::nearest(int requested) {
    if (requested <= 16) return 16;
    if (requested <= 32) return 32;
    if (requested <= 48) return 48;
    return 72;
}

const lv_font_t* Font::get(int size) {
    switch (nearest(size)) {
        case 16: return &Ubuntu_16px;
        case 32: return &Ubuntu_32px;
        case 48: return &Ubuntu_48px;
        case 72: return &Ubuntu_72px;
        default: return &Ubuntu_16px;
    }
}

const lv_font_t* Font::defaultFont() {
    return get(SMALL);
}

int Font::sizeOf(const lv_font_t* font) {
    if (font == &Ubuntu_32px) return MEDIUM;
    if (font == &Ubuntu_48px) return LARGE;
    if (font == &Ubuntu_72px) return XLARGE;
    return SMALL;
}

} // namespace UI
