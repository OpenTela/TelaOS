#include "utils/font.h"

namespace UI {

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

} // namespace UI
