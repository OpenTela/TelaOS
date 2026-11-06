/**
 * css_values.cpp — see css_values.h
 */
#include "ui/css_values.h"
#include "ui/css_colors.h"
#include "ui_layout.h"  // SCREEN_WIDTH, SCREEN_HEIGHT

#include <algorithm>
#include <array>
#include <cmath>

namespace UI {

namespace {

constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
constexpr bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
constexpr char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool equalsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && isSpace(s.front())) s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back())) s.remove_suffix(1);
    return s;
}

int hexDigit(char c) {
    if (isDigit(c)) return c - '0';
    c = lowerAscii(c);
    return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}

uint8_t clampByte(float v) {
    if (!(v > 0)) return 0;  // NaN too
    if (v >= 255) return 255;
    return static_cast<uint8_t>(v + 0.5f);
}

// RGB, RGBA, RRGGBB, RRGGBBAA (after the '#')
bool hexColor(std::string_view h, CssColor& out) {
    if (h.size() != 3 && h.size() != 4 && h.size() != 6 && h.size() != 8) return false;
    std::array<uint32_t, 8> d{};
    for (size_t i = 0; i < h.size(); ++i) {
        const int v = hexDigit(h[i]);
        if (v < 0) return false;
        d[i] = static_cast<uint32_t>(v);
    }
    if (h.size() <= 4) {  // #abc is #aabbcc
        out.rgb = (d[0] * 17) << 16 | (d[1] * 17) << 8 | d[2] * 17;
        out.alpha = h.size() == 4 ? static_cast<uint8_t>(d[3] * 17) : 255;
    } else {
        out.rgb = (d[0] << 4 | d[1]) << 16 | (d[2] << 4 | d[3]) << 8 | (d[4] << 4 | d[5]);
        out.alpha = h.size() == 8 ? static_cast<uint8_t>(d[6] << 4 | d[7]) : 255;
    }
    return true;
}

struct Component {  // one argument of rgb()/hsl(): 255, 50%, 120deg, none
    float value = 0;
    std::string_view unit;
    bool none = false;
};

// The arguments of rgb()/hsl(): with commas (1, 2, 3, 0.5) or spaces (1 2 3 / 0.5). The alpha, if
// there is one, is the 4th.
bool readComponents(std::string_view args, std::array<Component, 4>& c, size_t& count) {
    const bool commas = args.find(',') != std::string_view::npos;
    bool slash = false;
    bool needMore = true;  // at the start and after a comma or '/'
    count = 0;
    size_t i = 0;
    while (true) {
        while (i < args.size() && isSpace(args[i])) ++i;
        if (i >= args.size()) break;
        if (!commas && args[i] == '/') {
            if (slash || count != 3) return false;
            slash = true;
            needMore = true;
            ++i;
            continue;
        }
        if (count == 4 || (commas && !needMore)) return false;
        Component& k = c[count];
        if (!commas && i + 4 <= args.size() && equalsNoCase(args.substr(i, 4), "none")) {
            k.none = true;
            i += 4;
        } else {
            if (!cssNumber(args, i, k.value)) return false;
            const size_t us = i;
            while (i < args.size() && (isAlpha(args[i]) || args[i] == '%')) ++i;
            k.unit = args.substr(us, i - us);
        }
        ++count;
        needMore = false;
        while (i < args.size() && isSpace(args[i])) ++i;
        if (commas && i < args.size()) {
            if (args[i] != ',') return false;
            needMore = true;
            ++i;
        }
    }
    if (needMore) return false;              // "1, 2, 3," or "1 2 3 /"
    if (!commas && count == 4 && !slash) return false;  // the alpha comes after '/'
    return count == 3 || count == 4;
}

bool alphaOf(const Component& k, uint8_t& alpha) {
    if (k.none) {
        alpha = 0;
        return true;
    }
    if (k.unit.empty()) alpha = clampByte(k.value * 255);
    else if (k.unit == "%") alpha = clampByte(k.value * 2.55f);
    else return false;
    return true;
}

// rgb(), rgba(), hsl(), hsla()
bool functionColor(std::string_view name, std::string_view args, CssColor& out) {
    const bool rgb = equalsNoCase(name, "rgb") || equalsNoCase(name, "rgba");
    const bool hsl = equalsNoCase(name, "hsl") || equalsNoCase(name, "hsla");
    if (!rgb && !hsl) return false;
    std::array<Component, 4> c{};
    size_t count = 0;
    if (!readComponents(args, c, count)) return false;
    uint8_t alpha = 255;
    if (count == 4 && !alphaOf(c[3], alpha)) return false;
    std::array<float, 3> v{};  // 0..255
    if (rgb) {
        for (size_t k = 0; k < 3; ++k) {
            if (c[k].none) v[k] = 0;
            else if (c[k].unit.empty()) v[k] = c[k].value;
            else if (c[k].unit == "%") v[k] = c[k].value * 2.55f;
            else return false;
        }
    } else {
        float h = c[0].none ? 0 : c[0].value;  // degrees
        const std::string_view hu = c[0].unit;
        if (equalsNoCase(hu, "turn")) h *= 360;
        else if (equalsNoCase(hu, "rad")) h *= 57.29578f;
        else if (equalsNoCase(hu, "grad")) h *= 0.9f;
        else if (!hu.empty() && !equalsNoCase(hu, "deg")) return false;
        std::array<float, 2> sl{};  // 0..1
        for (size_t k = 1; k < 3; ++k) {
            if (!c[k].none && !c[k].unit.empty() && c[k].unit != "%") return false;
            sl[k - 1] = c[k].none ? 0 : std::min(std::max(c[k].value / 100, 0.0f), 1.0f);
        }
        h = std::fmod(h, 360.0f);
        if (h < 0) h += 360;
        const float s = sl[0], l = sl[1];
        const float a = s * std::min(l, 1 - l);
        const std::array<int, 3> n{0, 8, 4};  // CSS Color 4: hsl to rgb
        for (size_t k = 0; k < 3; ++k) {
            const float kk = std::fmod(n[k] + h / 30, 12.0f);
            v[k] = 255 * (l - a * std::max(-1.0f, std::min({kk - 3, 9 - kk, 1.0f})));
        }
    }
    out.rgb = static_cast<uint32_t>(clampByte(v[0])) << 16 | static_cast<uint32_t>(clampByte(v[1])) << 8 |
              clampByte(v[2]);
    out.alpha = alpha;
    return true;
}

}  // namespace

bool cssNumber(std::string_view s, size_t& i, float& v) {
    size_t j = i;
    bool neg = false;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) neg = s[j++] == '-';
    double val = 0;
    bool digits = false;
    while (j < s.size() && isDigit(s[j])) {
        val = val * 10 + (s[j++] - '0');
        digits = true;
    }
    if (j + 1 < s.size() && s[j] == '.' && isDigit(s[j + 1])) {
        ++j;
        double scale = 0.1;
        while (j < s.size() && isDigit(s[j])) {
            val += (s[j++] - '0') * scale;
            scale /= 10;
        }
        digits = true;
    }
    if (!digits) return false;
    if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {  // 1e3; the e of 1em is a unit
        size_t k = j + 1;
        bool eneg = false;
        if (k < s.size() && (s[k] == '+' || s[k] == '-')) eneg = s[k++] == '-';
        if (k < s.size() && isDigit(s[k])) {
            int e = 0;
            while (k < s.size() && isDigit(s[k])) e = std::min(e * 10 + (s[k++] - '0'), 99);
            val *= std::pow(10.0, eneg ? -e : e);
            j = k;
        }
    }
    v = static_cast<float>(neg ? -val : val);
    i = j;
    return true;
}

P::Array<std::string_view> cssParts(std::string_view value) {
    P::Array<std::string_view> parts;
    const size_t n = value.size();
    size_t i = 0;
    while (i < n) {
        while (i < n && isSpace(value[i])) ++i;
        if (i >= n) break;
        const size_t start = i;
        int depth = 0;
        char quote = 0;
        for (; i < n; ++i) {
            const char c = value[i];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                if (depth > 0) --depth;
            } else if (depth == 0 && isSpace(c)) {
                break;
            }
        }
        parts.push_back(value.substr(start, i - start));
    }
    return parts;
}

P::Array<std::string_view> cssList(std::string_view value) {
    P::Array<std::string_view> items;
    size_t start = 0;
    int depth = 0;
    char quote = 0;
    for (size_t i = 0; i <= value.size(); ++i) {
        const char c = i < value.size() ? value[i] : ',';
        if (quote) {
            if (c == quote) quote = 0;
            if (i < value.size()) continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (depth > 0) --depth;
        } else if (c == ',' && (depth == 0 || i == value.size())) {
            items.push_back(trimmed(value.substr(start, i - start)));
            start = i + 1;
        }
    }
    return items;
}

bool cssLength(std::string_view text, float em, int32_t& px) {
    text = trimmed(text);
    size_t i = 0;
    float v = 0;
    if (!cssNumber(text, i, v)) return false;
    const std::string_view unit = text.substr(i);
    float out = 0;
    if (unit.empty() || equalsNoCase(unit, "px")) out = v;
    else if (equalsNoCase(unit, "em")) out = v * em;
    else if (equalsNoCase(unit, "rem")) out = v * kCssRemPx;
    else if (equalsNoCase(unit, "vw")) out = v * SCREEN_WIDTH / 100.0f;
    else if (equalsNoCase(unit, "vh")) out = v * SCREEN_HEIGHT / 100.0f;
    else return false;
    px = static_cast<int32_t>(std::lround(out));
    return true;
}

bool cssPercent(std::string_view text, float& v) {
    text = trimmed(text);
    size_t i = 0;
    return !text.empty() && text.back() == '%' && cssNumber(text, i, v) && i == text.size() - 1;
}

bool cssCoord(std::string_view text, float em, int32_t screen, int32_t& px) {
    float v = 0;
    if (cssPercent(text, v)) {
        px = static_cast<int32_t>(std::lround(v * static_cast<float>(screen) / 100));
        return true;
    }
    return cssLength(text, em, px);
}

int cssFontSize(std::string_view text, float parentPx) {
    text = trimmed(text);
    float v = 0;
    if (!text.empty() && text.back() == '%') {
        size_t i = 0;
        if (!cssNumber(text, i, v) || i != text.size() - 1) return 0;
        const long px = std::lround(v * parentPx / 100);
        return px > 0 ? static_cast<int>(px) : 0;
    }
    int32_t px = 0;
    if (!cssLength(text, parentPx, px)) return 0;
    return px > 0 ? static_cast<int>(px) : 0;
}

int cssFontWeight(std::string_view text) {
    text = trimmed(text);
    if (equalsNoCase(text, "normal") || equalsNoCase(text, "lighter")) return 400;
    if (equalsNoCase(text, "bold") || equalsNoCase(text, "bolder")) return 700;
    size_t i = 0;
    float v = 0;
    if (!cssNumber(text, i, v) || i != text.size() || v < 1 || v > 1000) return 0;
    return static_cast<int>(v);
}

bool cssColor(std::string_view text, CssColor& out) {
    text = trimmed(text);
    if (text.empty()) return false;
    if (text[0] == '#') return hexColor(text.substr(1), out);
    if (equalsNoCase(text, "transparent")) {
        out = CssColor{0x000000, 0};
        return true;
    }
    const size_t open = text.find('(');
    if (open != std::string_view::npos) {
        if (open == 0 || text.back() != ')') return false;
        return functionColor(trimmed(text.substr(0, open)), text.substr(open + 1, text.size() - open - 2), out);
    }
    uint32_t rgb = 0;
    if (!cssNamedColor(text, rgb)) return false;
    out = CssColor{rgb, 255};
    return true;
}

}  // namespace UI
