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

constexpr int kSizes[] = {Font::SMALL, Font::MEDIUM, Font::LARGE, Font::XLARGE};

// The place of a size of get() in kSizes: the next one not smaller
int slot(int px) {
    return px <= Font::SMALL ? 0 : px <= Font::MEDIUM ? 1 : px <= Font::LARGE ? 2 : 3;
}

// The icon names of Icons.txt, sorted by name as the file is: name → code
P::Array<std::pair<P::String, uint32_t>> s_icons;

#if defined(LVGL_MOCK_ENABLED)
// No files in the mock: stand-ins of the bold faces, so tests see which face the engine asks for
lv_font_t s_mockBold[4] = {{Font::SMALL}, {Font::MEDIUM}, {Font::LARGE}, {Font::XLARGE}};

const lv_font_t* boldFont(int i) { return &s_mockBold[i]; }
const lv_font_t* loadedBold(int i) { return &s_mockBold[i]; }
#else
// A glyph Ubuntu_*px does not have (ä, Ł, Ω, Ґ, №, ≤…) is drawn from UbuntuExt_*px of the
// same size. Set before main(): the launcher takes &Ubuntu_*px directly, not through get().
const bool s_fallbacks = [] {
    Ubuntu_16px.fallback = &UbuntuExt_16px;
    Ubuntu_32px.fallback = &UbuntuExt_32px;
    Ubuntu_48px.fallback = &UbuntuExt_48px;
    Ubuntu_72px.fallback = &UbuntuExt_72px;
    return true;
}();

// The fonts of files, by size: the icons (loadFiles) and the bold faces (loaded when first asked
// for; tried once, so a file that is not there stays nullptr)
lv_font_t* s_iconFonts[4] = {};
lv_font_t* s_bold[4] = {};
bool s_boldTried[4] = {};

// A font file takes up to 400 KB of PSRAM: loaded only with room for it and much to spare
constexpr size_t kFilesRoom = 1024 * 1024;

// data/system/fonts/<file> (lv_binfont_create), into PSRAM: fonts stay loaded for good, and DRAM
// is the heap that runs short. Without room there (a board without PSRAM) the file is left out:
// LVGL stops on an allocation that fails.
lv_font_t* loadFont(const P::String& file) {
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < kFilesRoom) {
        LOG_W(Log::UI, "font: no room in PSRAM for %s", file.c_str());
        return nullptr;
    }
    const uint32_t start = millis();
    lv_font_t* font = nullptr;
    {
        MemoryManager::InPsram inPsram;
        font = lv_binfont_create((P::String(SYS_LVGL_PREFIX SYS_FONTS) + file).c_str());
    }
    if (!font) {
        LOG_W(Log::UI, "font: no " SYS_FONTS "%s (pio run -t uploadfs)", file.c_str());
        return nullptr;
    }
    LOG_I(Log::UI, "font: %s in %u ms", file.c_str(), static_cast<unsigned>(millis() - start));
    return font;
}

P::String fileName(const char* face, int px) {
    return P::String(face) + "_" + std::to_string(px).c_str() + "px.bin";
}

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

// UbuntuBold_<px>px.bin, the bold face of a size, the first time text of that size is bold. Its
// glyphs are those of Ubuntu_*px and UbuntuExt_*px together; the icons of the size are behind it.
const lv_font_t* boldFont(int i) {
    if (!s_boldTried[i]) {
        s_boldTried[i] = true;
        s_bold[i] = loadFont(fileName("UbuntuBold", kSizes[i]));
        if (s_bold[i]) s_bold[i]->fallback = s_iconFonts[i];
    }
    return s_bold[i];
}

const lv_font_t* loadedBold(int i) { return s_bold[i]; }
#endif

}  // namespace

void Font::loadFiles() {
#if !defined(LVGL_MOCK_ENABLED)
    lv_font_t* text[] = {&Ubuntu_16px, &Ubuntu_32px, &Ubuntu_48px, &Ubuntu_72px};
    const lv_font_t* ext[] = {&UbuntuExt_16px, &UbuntuExt_32px, &UbuntuExt_48px, &UbuntuExt_72px};
    for (int i = 0; i < 4; ++i) {
        lv_font_t* icons = loadFont(fileName("Icons", kSizes[i]));
        if (!icons) continue;
        // Icons hold codes no text font has (their own, ✓ ← ⚙): text → icons → other languages
        icons->fallback = ext[i];
        text[i]->fallback = icons;
        s_iconFonts[i] = icons;
    }
    loadIconNames();
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
    return kSizes[slot(requested)];
}

const lv_font_t* Font::get(int size, bool bold) {
    const int i = slot(size);
    if (bold) {
        if (const lv_font_t* b = boldFont(i)) return b;  // no file: the regular face
    }
    switch (i) {
        case 1: return &Ubuntu_32px;
        case 2: return &Ubuntu_48px;
        case 3: return &Ubuntu_72px;
        default: return &Ubuntu_16px;
    }
}

const lv_font_t* Font::defaultFont() {
    return get(SMALL);
}

int Font::sizeOf(const lv_font_t* font) {
    for (int i = 0; i < 4; ++i) {
        if (font && font == loadedBold(i)) return kSizes[i];
    }
    if (font == &Ubuntu_32px) return MEDIUM;
    if (font == &Ubuntu_48px) return LARGE;
    if (font == &Ubuntu_72px) return XLARGE;
    return SMALL;
}

bool Font::isBold(const lv_font_t* font) {
    for (int i = 0; i < 4; ++i) {
        if (font && font == loadedBold(i)) return true;
    }
    return false;
}

} // namespace UI
