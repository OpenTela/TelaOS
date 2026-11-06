#include "utils/font.h"

#include "utils/psram_alloc.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
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

// The sizes get() draws. 16, 32, 48 and 72 are Ubuntu_*px, in flash; the ones between are files on
// LittleFS, Ubuntu_<px>px.bin (scripts/make_fonts.py, the same list), loaded the first time text of
// the size is drawn.
constexpr int kSizes[] = {12, 14, Font::SMALL, 18, 20, 24, 28, Font::MEDIUM, 36, 40, Font::LARGE, Font::XLARGE};
constexpr int kCount = static_cast<int>(sizeof(kSizes) / sizeof(kSizes[0]));

bool inFlash(int i) {
    const int px = kSizes[i];
    return px == Font::SMALL || px == Font::MEDIUM || px == Font::LARGE || px == Font::XLARGE;
}

// The place in kSizes of the size nearest px, the larger of two as near (22 → 24); flashOnly: of the
// four in flash. No size (0): the default, 16.
int slot(int px, bool flashOnly = false) {
    if (px <= 0) px = Font::SMALL;
    int best = -1;
    for (int i = 0; i < kCount; ++i) {
        if (flashOnly && !inFlash(i)) continue;
        if (best < 0 || std::abs(kSizes[i] - px) <= std::abs(kSizes[best] - px)) best = i;
    }
    return best;
}

// Ubuntu_*px of a size in flash
lv_font_t* flashFont(int i) {
    switch (kSizes[i]) {
        case Font::MEDIUM: return &Ubuntu_32px;
        case Font::LARGE: return &Ubuntu_48px;
        case Font::XLARGE: return &Ubuntu_72px;
        default: return &Ubuntu_16px;
    }
}

// The icon names of Icons.txt, sorted by name as the file is: name → code
P::Array<std::pair<P::String, uint32_t>> s_icons;

#if defined(LVGL_MOCK_ENABLED)
// No files in the mock: stand-ins of the fonts kept as files, so tests see which font the engine
// asks for. A stand-in's line_height is its size, as with the mock's Ubuntu_*px.
lv_font_t s_mockRegular[kCount];
lv_font_t s_mockBold[kCount];
const bool s_mock = [] {
    for (int i = 0; i < kCount; ++i) s_mockRegular[i].line_height = s_mockBold[i].line_height = kSizes[i];
    return true;
}();

const lv_font_t* regularFont(int i) { return inFlash(i) ? flashFont(i) : &s_mockRegular[i]; }
const lv_font_t* boldFont(int i) { return &s_mockBold[i]; }
const lv_font_t* loadedRegular(int i) { return regularFont(i); }
const lv_font_t* loadedBold(int i) { return &s_mockBold[i]; }
bool hasFile(int) { return true; }
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

// The fonts of files of a size (by its place in kSizes), each tried once: a file that is not there
// stays nullptr
struct Files {
    lv_font_t* regular = nullptr;  // a size between the four in flash: Ubuntu_<px>px.bin
    lv_font_t* bold = nullptr;     // UbuntuBold_<px>px.bin
    lv_font_t* icons = nullptr;    // Icons_<px>px.bin: of the four in flash at start, the rest with their text
    bool regularTried = false;
    bool boldTried = false;
    bool iconsTried = false;
};
Files s_files[kCount];

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

lv_font_t* loadIcons(int i) {
    Files& f = s_files[i];
    if (!f.iconsTried) {
        f.iconsTried = true;
        f.icons = loadFont(fileName("Icons", kSizes[i]));
    }
    return f.icons;
}

// The icons behind the text of a size; for a size between whose file is not there, those of the
// nearest size in flash
const lv_font_t* iconsFont(int i) {
    if (const lv_font_t* icons = loadIcons(i)) return icons;
    return inFlash(i) ? nullptr : s_files[slot(kSizes[i], true)].icons;
}

// Ubuntu_<px>px.bin, a size between the four in flash, the first time text of the size is drawn:
// the glyphs of Ubuntu_*px and UbuntuExt_*px together, the icons of the size behind them
const lv_font_t* regularFont(int i) {
    if (inFlash(i)) return flashFont(i);
    Files& f = s_files[i];
    if (!f.regularTried) {
        f.regularTried = true;
        f.regular = loadFont(fileName("Ubuntu", kSizes[i]));
        if (f.regular) f.regular->fallback = iconsFont(i);
    }
    return f.regular;
}

// UbuntuBold_<px>px.bin, the bold face of a size, the first time text of the size is bold: the
// same glyphs, the icons of the size behind them
const lv_font_t* boldFont(int i) {
    Files& f = s_files[i];
    if (!f.boldTried) {
        f.boldTried = true;
        f.bold = loadFont(fileName("UbuntuBold", kSizes[i]));
        if (f.bold) f.bold->fallback = iconsFont(i);
    }
    return f.bold;
}

const lv_font_t* loadedRegular(int i) { return inFlash(i) ? flashFont(i) : s_files[i].regular; }
const lv_font_t* loadedBold(int i) { return s_files[i].bold; }
// Whether text of a size draws in it: in flash, loaded, or not tried yet
bool hasFile(int i) { return inFlash(i) || !s_files[i].regularTried || s_files[i].regular; }
#endif

}  // namespace

void Font::loadFiles() {
#if !defined(LVGL_MOCK_ENABLED)
    for (int i = 0; i < kCount; ++i) {
        if (!inFlash(i)) continue;  // the sizes between load their icons with their text
        lv_font_t* icons = loadIcons(i);
        if (!icons) continue;
        // Icons hold codes no text font has (their own, ✓ ← ⚙): text → icons → other languages
        lv_font_t* text = flashFont(i);
        if (text->fallback == icons) continue;  // chained already
        icons->fallback = text->fallback;       // UbuntuExt_*px (s_fallbacks)
        text->fallback = icons;
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
    const int i = slot(requested);
    return kSizes[hasFile(i) ? i : slot(requested, true)];
}

const lv_font_t* Font::get(int size, bool bold) {
    // The nearest size; without its file, the nearest of the four in flash. Of a size, the bold
    // face, or the regular one without its file.
    for (const int i : {slot(size), slot(size, true)}) {
        if (bold) {
            if (const lv_font_t* b = boldFont(i)) return b;
        }
        if (const lv_font_t* r = regularFont(i)) return r;
    }
    return &Ubuntu_16px;  // not reached: the sizes in flash are always there
}

const lv_font_t* Font::defaultFont() {
    return get(SMALL);
}

int Font::sizeOf(const lv_font_t* font) {
    if (!font) return SMALL;
    for (int i = 0; i < kCount; ++i) {
        if (font == loadedRegular(i) || font == loadedBold(i)) return kSizes[i];
    }
    return SMALL;
}

bool Font::isBold(const lv_font_t* font) {
    if (!font) return false;
    for (int i = 0; i < kCount; ++i) {
        if (font == loadedBold(i)) return true;
    }
    return false;
}

} // namespace UI
