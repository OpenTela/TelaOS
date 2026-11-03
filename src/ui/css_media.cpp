/**
 * css_media.cpp — see css_media.h.
 *
 * Media Queries 4, the part that means something on a watch: media types (all, screen; print
 * and the others never hold), not / only, and / or / not in conditions, features with a value
 * (width: 480px), min- and max- forms, ranges (400px <= width < 800px) and the boolean form
 * ((hover)). A feature this engine does not know is unknown, and so is the query around it:
 * unknown holds as false, `not` of it too, as in a browser.
 */
#include "ui/css_media.h"
#include "ui/css_values.h"
#include "ui_layout.h"  // SCREEN_WIDTH, SCREEN_HEIGHT
#include "lvgl.h"

#include <cmath>

namespace UI {

namespace {

constexpr size_t npos = std::string_view::npos;

char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

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

// Three-valued, as Media Queries 4: a feature this engine does not know is unknown
enum class Tri : uint8_t { False, True, Unknown };

Tri tri(bool b) { return b ? Tri::True : Tri::False; }
Tri triNot(Tri t) { return t == Tri::Unknown ? t : tri(t == Tri::False); }
Tri triAnd(Tri a, Tri b) {
    if (a == Tri::False || b == Tri::False) return Tri::False;
    return (a == Tri::True && b == Tri::True) ? Tri::True : Tri::Unknown;
}
Tri triOr(Tri a, Tri b) {
    if (a == Tri::True || b == Tri::True) return Tri::True;
    return (a == Tri::False && b == Tri::False) ? Tri::False : Tri::Unknown;
}

// ---- Values ----

// 480px, 30em (16px), 0; false for anything else
bool lengthValue(std::string_view v, float& px) {
    v = trimmed(v);
    size_t i = 0;
    float n = 0;
    if (!cssNumber(v, i, n)) return false;
    const std::string_view u = v.substr(i);
    if (equalsNoCase(u, "px") || (u.empty() && n == 0)) px = n;
    else if (equalsNoCase(u, "em") || equalsNoCase(u, "rem")) px = n * kCssRemPx;
    else return false;
    return true;
}

// 16/9, 16 / 9, 1.5
bool ratioValue(std::string_view v, float& r) {
    v = trimmed(v);
    const size_t slash = v.find('/');
    size_t i = 0;
    float a = 0, b = 1;
    const std::string_view first = trimmed(v.substr(0, slash));
    if (!cssNumber(first, i, a) || i != first.size()) return false;
    if (slash != npos) {
        const std::string_view second = trimmed(v.substr(slash + 1));
        i = 0;
        if (!cssNumber(second, i, b) || i != second.size() || b <= 0) return false;
    }
    r = a / b;
    return true;
}

// 130dpi, 2dppx, 2x, 50dpcm: in dots per inch
bool resolutionValue(std::string_view v, float& dpi) {
    v = trimmed(v);
    size_t i = 0;
    float n = 0;
    if (!cssNumber(v, i, n)) return false;
    const std::string_view u = v.substr(i);
    if (equalsNoCase(u, "dpi")) dpi = n;
    else if (equalsNoCase(u, "dppx") || equalsNoCase(u, "x")) dpi = n * 96;
    else if (equalsNoCase(u, "dpcm")) dpi = n * 2.54f;
    else return false;
    return true;
}

bool integerValue(std::string_view v, float& n) {
    v = trimmed(v);
    size_t i = 0;
    return cssNumber(v, i, n) && i == v.size() && n == std::floor(n);
}

// ---- Features ----

enum class Kind : uint8_t { Length, Ratio, Resolution, Integer, Word };

struct Feature {
    Kind kind = Kind::Word;
    float number = 0;           // Length, Ratio, Resolution, Integer
    std::string_view word;      // Word: the screen's one value
    bool rangeable = false;     // min-/max- and <, >
};

// The screen's value of a feature; false if this engine does not know it
bool feature(std::string_view name, const CssScreen& s, Feature& f) {
    auto word = [&f](std::string_view w) {
        f.kind = Kind::Word;
        f.word = w;
        return true;
    };
    auto number = [&f](Kind k, float v) {
        f.kind = k;
        f.number = v;
        f.rangeable = true;
        return true;
    };
    if (equalsNoCase(name, "width") || equalsNoCase(name, "device-width")) return number(Kind::Length, s.width);
    if (equalsNoCase(name, "height") || equalsNoCase(name, "device-height")) return number(Kind::Length, s.height);
    if (equalsNoCase(name, "aspect-ratio") || equalsNoCase(name, "device-aspect-ratio")) {
        return number(Kind::Ratio, s.height > 0 ? s.width / s.height : 0);
    }
    if (equalsNoCase(name, "resolution")) return number(Kind::Resolution, s.dpi);
    if (equalsNoCase(name, "color")) return number(Kind::Integer, LV_COLOR_DEPTH >= 24 ? 8 : 5);  // RGB565: 5 bits
    if (equalsNoCase(name, "monochrome") || equalsNoCase(name, "color-index") || equalsNoCase(name, "grid")) {
        return number(Kind::Integer, 0);
    }
    if (equalsNoCase(name, "orientation")) return word(s.height >= s.width ? "portrait" : "landscape");
    if (equalsNoCase(name, "hover") || equalsNoCase(name, "any-hover")) return word("none");
    if (equalsNoCase(name, "pointer") || equalsNoCase(name, "any-pointer")) return word("coarse");
    if (equalsNoCase(name, "prefers-color-scheme")) return word("dark");  // LV_THEME_DEFAULT_DARK
    if (equalsNoCase(name, "prefers-reduced-motion") || equalsNoCase(name, "prefers-contrast") ||
        equalsNoCase(name, "prefers-reduced-transparency")) {
        return word("no-preference");
    }
    if (equalsNoCase(name, "forced-colors") || equalsNoCase(name, "inverted-colors")) return word("none");
    if (equalsNoCase(name, "shape")) return word("rect");
    if (equalsNoCase(name, "scan")) return word("progressive");
    if (equalsNoCase(name, "update")) return word("fast");
    if (equalsNoCase(name, "overflow-block")) return word("scroll");
    if (equalsNoCase(name, "overflow-inline")) return word("none");
    return false;
}

// A value of the feature's kind
bool valueOf(const Feature& f, std::string_view text, float& v) {
    switch (f.kind) {
        case Kind::Length: return lengthValue(text, v);
        case Kind::Ratio: return ratioValue(text, v);
        case Kind::Resolution: return resolutionValue(text, v);
        case Kind::Integer: return integerValue(text, v);
        case Kind::Word: return false;
    }
    return false;
}

// The boolean form, (hover): true unless the value is 0 or none
bool truthy(const Feature& f) { return f.kind == Kind::Word ? f.word != "none" : f.number != 0; }

// a op b with op one of < <= > >= =
bool compare(float a, std::string_view op, float b) {
    constexpr float eps = 1e-4f;  // 16/9 and 1.7777
    if (op == "<") return a < b - eps;
    if (op == "<=") return a <= b + eps;
    if (op == ">") return a > b + eps;
    if (op == ">=") return a >= b - eps;
    return std::fabs(a - b) <= eps;  // =
}

// The operator at i (and i past it); "" if none
std::string_view readOp(std::string_view s, size_t& i) {
    while (i < s.size() && isSpace(s[i])) ++i;
    if (i >= s.size()) return {};
    const size_t start = i;
    if (s[i] == '<' || s[i] == '>') {
        ++i;
        if (i < s.size() && s[i] == '=') ++i;
    } else if (s[i] == '=') {
        ++i;
    }
    return s.substr(start, i - start);
}

bool isNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// What is inside (...): name, name: value, or a range
Tri mediaFeature(std::string_view text, const CssScreen& s) {
    text = trimmed(text);
    const size_t colon = text.find(':');
    if (colon != npos) {  // name: value, min-name: value
        std::string_view name = trimmed(text.substr(0, colon));
        const std::string_view value = trimmed(text.substr(colon + 1));
        int bound = 0;  // -1 max-, 1 min-
        if (name.size() > 4 && equalsNoCase(name.substr(0, 4), "min-")) bound = 1, name.remove_prefix(4);
        else if (name.size() > 4 && equalsNoCase(name.substr(0, 4), "max-")) bound = -1, name.remove_prefix(4);
        Feature f;
        if (!feature(name, s, f)) return Tri::Unknown;
        if (f.kind == Kind::Word) return bound ? Tri::Unknown : tri(equalsNoCase(value, f.word));
        if (bound && !f.rangeable) return Tri::Unknown;
        float v = 0;
        if (!valueOf(f, value, v)) return Tri::Unknown;
        return tri(compare(f.number, bound > 0 ? ">=" : bound < 0 ? "<=" : "=", v));
    }
    // A range: width >= 400px, 400px <= width < 800px; or the boolean form: (color)
    size_t i = 0;
    while (i < text.size() && !isSpace(text[i]) && text[i] != '<' && text[i] != '>' && text[i] != '=') ++i;
    const std::string_view first = text.substr(0, i);
    const std::string_view op1 = readOp(text, i);
    if (op1.empty()) {
        if (i < text.size()) return Tri::Unknown;  // something that does not read
        Feature f;
        if (!feature(first, s, f)) return Tri::Unknown;
        return tri(truthy(f));
    }
    while (i < text.size() && isSpace(text[i])) ++i;
    size_t j = i;
    while (j < text.size() && !isSpace(text[j]) && text[j] != '<' && text[j] != '>' && text[j] != '=') ++j;
    const std::string_view second = text.substr(i, j - i);
    const std::string_view op2 = readOp(text, j);
    Feature f;
    float v = 0;
    if (op2.empty()) {  // name op value, or value op name
        if (j < text.size()) return Tri::Unknown;
        if (feature(first, s, f)) {
            if (!f.rangeable || !valueOf(f, second, v)) return Tri::Unknown;
            return tri(compare(f.number, op1, v));
        }
        if (feature(second, s, f)) {
            if (!f.rangeable || !valueOf(f, first, v)) return Tri::Unknown;
            return tri(compare(v, op1, f.number));
        }
        return Tri::Unknown;
    }
    // value op name op value: both the same way round (< <, or > >)
    if ((op1[0] == '<') != (op2[0] == '<') || op1[0] == '=' || op2[0] == '=') return Tri::Unknown;
    const std::string_view third = trimmed(text.substr(j));
    float lo = 0, hi = 0;
    if (!feature(second, s, f) || !f.rangeable || !valueOf(f, first, lo) || !valueOf(f, third, hi)) {
        return Tri::Unknown;
    }
    return tri(compare(lo, op1, f.number) && compare(f.number, op2, hi));
}

// The position of the ')' that closes the '(' at i; npos if there is none
size_t closing(std::string_view s, size_t i) {
    int depth = 0;
    for (; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')' && --depth == 0) return i;
    }
    return npos;
}

// The word at i (letters, digits, '-'), i past it
std::string_view readWord(std::string_view s, size_t& i) {
    while (i < s.size() && isSpace(s[i])) ++i;
    const size_t start = i;
    while (i < s.size() && isNameChar(s[i])) ++i;
    return s.substr(start, i - start);
}

Tri condition(std::string_view s, size_t& i, const CssScreen& screen, bool allowOr, bool& ok);

// ( condition ) or ( feature ) at i
Tri inParens(std::string_view s, size_t& i, const CssScreen& screen, bool& ok) {
    while (i < s.size() && isSpace(s[i])) ++i;
    if (i >= s.size() || s[i] != '(') {
        ok = false;
        return Tri::Unknown;
    }
    const size_t close = closing(s, i);
    if (close == npos) {
        ok = false;
        return Tri::Unknown;
    }
    const std::string_view inner = s.substr(i + 1, close - i - 1);
    i = close + 1;
    const std::string_view t = trimmed(inner);
    size_t k = 0;
    const std::string_view w = readWord(t, k);
    if ((!t.empty() && t[0] == '(') || (equalsNoCase(w, "not") && k < t.size())) {
        size_t j = 0;
        bool inner_ok = true;
        const Tri r = condition(t, j, screen, true, inner_ok);
        while (j < t.size() && isSpace(t[j])) ++j;
        return inner_ok && j == t.size() ? r : Tri::Unknown;  // (general enclosed): unknown
    }
    return mediaFeature(inner, screen);
}

// not (a) | (a) and (b) and ... | (a) or (b) or ...
Tri condition(std::string_view s, size_t& i, const CssScreen& screen, bool allowOr, bool& ok) {
    size_t k = i;
    if (equalsNoCase(readWord(s, k), "not")) {
        i = k;
        return triNot(inParens(s, i, screen, ok));
    }
    Tri r = inParens(s, i, screen, ok);
    std::string_view joiner;
    while (ok) {
        k = i;
        const std::string_view w = readWord(s, k);
        if (w.empty()) break;
        const bool isAnd = equalsNoCase(w, "and"), isOr = equalsNoCase(w, "or");
        if ((!isAnd && !isOr) || (isOr && !allowOr) || (!joiner.empty() && !equalsNoCase(w, joiner))) {
            ok = false;  // a word that does not belong, or and mixed with or without parentheses
            break;
        }
        joiner = w;
        i = k;
        const Tri next = inParens(s, i, screen, ok);
        r = isAnd ? triAnd(r, next) : triOr(r, next);
    }
    return r;
}

// One query of a list: a condition, or [not | only] type [and condition]
bool mediaQuery(std::string_view q, const CssScreen& screen) {
    q = trimmed(q);
    if (q.empty()) return false;
    size_t i = 0;
    bool ok = true;
    size_t k = 0;
    const std::string_view first = readWord(q, k);
    size_t after = k;
    while (after < q.size() && isSpace(q[after])) ++after;
    if (q[0] == '(' || (equalsNoCase(first, "not") && after < q.size() && q[after] == '(')) {
        const Tri r = condition(q, i, screen, true, ok);
        while (i < q.size() && isSpace(q[i])) ++i;
        return ok && i == q.size() && r == Tri::True;
    }
    bool negate = false;
    std::string_view type = readWord(q, i);
    if (equalsNoCase(type, "not")) {
        negate = true;
        type = readWord(q, i);
    } else if (equalsNoCase(type, "only")) {
        type = readWord(q, i);
    }
    if (type.empty()) return false;
    Tri r = tri(equalsNoCase(type, "all") || equalsNoCase(type, "screen"));  // print, speech...: no
    k = i;
    const std::string_view w = readWord(q, k);
    if (!w.empty()) {
        if (!equalsNoCase(w, "and")) return false;
        i = k;
        r = triAnd(r, condition(q, i, screen, false, ok));
    }
    while (i < q.size() && isSpace(q[i])) ++i;
    if (!ok || i != q.size()) return false;  // does not read: not all
    if (r == Tri::Unknown) return false;
    return negate ? r == Tri::False : r == Tri::True;
}

}  // namespace

CssScreen cssScreen() {
    CssScreen s;
    s.width = SCREEN_WIDTH;
    s.height = SCREEN_HEIGHT;
    s.dpi = LV_DPI_DEF;
    return s;
}

bool cssMediaMatches(std::string_view list, const CssScreen& screen) {
    list = trimmed(list);
    if (list.empty()) return true;  // @media { }: all
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i <= list.size(); ++i) {
        const char c = i < list.size() ? list[i] : ',';
        if (c == '(') ++depth;
        else if (c == ')' && depth > 0) --depth;
        else if (c == ',' && depth == 0) {
            if (mediaQuery(list.substr(start, i - start), screen)) return true;
            start = i + 1;
        }
    }
    return false;
}

}  // namespace UI
