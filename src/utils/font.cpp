#include "utils/font.h"

#include "utils/psram_alloc.h"

#include <algorithm>
#include <string>
#include <utility>

#if !defined(LVGL_MOCK_ENABLED)
#include <Arduino.h>
#include <LittleFS.h>

#include "core/sys_paths.h"
#include "esp_heap_caps.h"
#include "utils/log_config.h"
#include "utils/memory_manager.h"

static const char* TAG = "font";
#endif

namespace UI {

namespace {

#if !defined(LVGL_MOCK_ENABLED)
// A glyph Ubuntu_*px does not have (ä, Ł, Ω, Ґ, №, ≤…) is drawn from UbuntuExt_*px of the
// same size. Set before main(): the launcher takes &Ubuntu_*px directly, not through get().
const bool s_fallbacks = [] {
    Ubuntu_16px.fallback = &UbuntuExt_16px;
    Ubuntu_32px.fallback = &UbuntuExt_32px;
    Ubuntu_48px.fallback = &UbuntuExt_48px;
    Ubuntu_72px.fallback = &UbuntuExt_72px;
    return true;
}();
#endif

// The icon names of Icons.txt, sorted by name as the file is: name → code
P::Array<std::pair<P::String, uint32_t>> s_icons;

#if !defined(LVGL_MOCK_ENABLED)
// The four icon fonts take 300 KB of PSRAM: loaded only with room for them and some to spare
constexpr size_t kFilesRoom = 1024 * 1024;

// Icons.txt: a line per icon, "home e9b2"
void loadIconNames() {
    File f = LittleFS.open(SYS_FONTS "Icons.txt", "r");
    if (!f) {
        LOG_W(Log::UI, "font: no " SYS_FONTS "Icons.txt: icons by name show their names");
        return;
    }
    P::String text(f.size(), '\0');
    f.readBytes(&text[0], text.size());
    f.close();
    std::string_view rest(text);
    while (!rest.empty()) {
        const size_t eol = std::min(rest.find('\n'), rest.size());
        const std::string_view line = rest.substr(0, eol);
        rest.remove_prefix(std::min(eol + 1, rest.size()));
        const size_t sp = line.find(' ');
        if (sp == std::string_view::npos) continue;
        uint32_t code = 0;
        for (char c : line.substr(sp + 1)) {
            const int digit = (c >= '0' && c <= '9') ? c - '0'
                            : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                            : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (digit < 0) break;
            code = code * 16 + static_cast<uint32_t>(digit);
        }
        if (code) s_icons.emplace_back(P::String(line.substr(0, sp)), code);
    }
    std::sort(s_icons.begin(), s_icons.end());  // the file is sorted; a hand-edited one may not be
}
#endif

}  // namespace

void Font::loadFiles() {
#if !defined(LVGL_MOCK_ENABLED)
    // In PSRAM: they stay loaded for good, and DRAM is the heap that runs short. Without room
    // there (a board without PSRAM) they are left out: LVGL stops on an allocation that fails.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < kFilesRoom) {
        LOG_W(Log::UI, "font: no room in PSRAM, icon fonts not loaded");
        return;
    }
    MemoryManager& memory = MemoryManager::instance();
    const MemoryManager::Mode mode = memory.mode();
    memory.setMode(MemoryManager::PSRAM);
    struct Size {
        int px;
        lv_font_t& text;
        const lv_font_t& ext;
    };
    const Size sizes[] = {{SMALL, Ubuntu_16px, UbuntuExt_16px}, {MEDIUM, Ubuntu_32px, UbuntuExt_32px},
                          {LARGE, Ubuntu_48px, UbuntuExt_48px}, {XLARGE, Ubuntu_72px, UbuntuExt_72px}};
    for (const Size& s : sizes) {
        const P::String file = P::String("Icons_") + std::to_string(s.px).c_str() + "px.bin";
        const uint32_t start = millis();
        lv_font_t* icons = lv_binfont_create((P::String(SYS_LVGL_PREFIX SYS_FONTS) + file).c_str());
        if (!icons) {
            LOG_W(Log::UI, "font: no " SYS_FONTS "%s (pio run -t uploadfs): no icons at %dpx", file.c_str(), s.px);
            continue;
        }
        // Icons hold codes no text font has (their own, ✓ ← ⚙): text → icons → other languages
        icons->fallback = &s.ext;
        s.text.fallback = icons;
        LOG_I(Log::UI, "font: %s in %u ms", file.c_str(), static_cast<unsigned>(millis() - start));
    }
    loadIconNames();
    memory.setMode(mode);
    LOG_I(Log::UI, "font: %u icon names", static_cast<unsigned>(s_icons.size()));
#endif
}

uint32_t Font::iconCode(std::string_view name) {
    const auto it = std::lower_bound(s_icons.begin(), s_icons.end(), name,
                                     [](const std::pair<P::String, uint32_t>& e, std::string_view n) {
                                         return std::string_view(e.first) < n;
                                     });
    return it != s_icons.end() && std::string_view(it->first) == name ? it->second : 0;
}

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
