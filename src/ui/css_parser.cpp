/**
 * css_parser.cpp - CSS parser with tag, class, id selectors
 *
 * Selector types and specificity (low → high):
 *   button       { ... }   — tag       (specificity 0)
 *   .primary     { ... }   — class     (specificity 1)
 *   button.primary { ... } — tag+class (specificity 2)
 *   #myBtn       { ... }   — id        (specificity 3)
 *
 * Cascade: rules applied in specificity order.
 * Within same specificity, later rules win.
 * Inline HTML attributes always win over CSS.
 */

#include "ui/css_parser.h"

// From ui_widget_builder.cpp — parse "17%", "50", "12.5%" → pixels
int32_t parse_coord_w(const char* s);
int32_t parse_coord_h(const char* s);

#include "widgets/widget_common.h"
#include "utils/log_config.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

static const char* TAG = "CSS";

// From ui_html.cpp — stores z-index on Element for post-render pass
void setElementZIndex(lv_obj_t* handle, int z);

namespace UI {

const P::Map<P::String, P::String> Css::s_empty;
const P::String Css::s_emptyStr;

Css& Css::instance() {
    static Css s_instance;
    return s_instance;
}

void Css::clear() {
    m_rules.clear();
}

P::String Css::trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return P::String(s.data() + start, end - start);
}

// ============ Selector parsing ============

static void parseSelectorStr(const P::String& sel, SelectorType& type,
                              P::String& tag, P::String& cls, P::String& id) {
    tag.clear(); cls.clear(); id.clear();
    if (sel.empty()) { type = SelectorType::TAG; return; }

    if (sel[0] == '#') {
        type = SelectorType::ID;
        id = P::String(sel.data() + 1, sel.size() - 1);
    } else if (sel[0] == '.') {
        type = SelectorType::CLASS;
        cls = P::String(sel.data() + 1, sel.size() - 1);
    } else {
        auto dot = sel.find('.');
        if (dot != P::String::npos) {
            type = SelectorType::TAG_CLASS;
            tag = P::String(sel.data(), dot);
            cls = P::String(sel.data() + dot + 1, sel.size() - dot - 1);
        } else {
            type = SelectorType::TAG;
            tag = sel;
        }
    }
}

// ============ Parse properties block ============

static P::Map<P::String, P::String> parseProperties(std::string_view block) {
    P::Map<P::String, P::String> props;
    size_t pos = 0;
    while (pos < block.size()) {
        while (pos < block.size() && std::isspace(static_cast<unsigned char>(block[pos]))) pos++;
        if (pos >= block.size()) break;

        size_t nameStart = pos;
        while (pos < block.size() && block[pos] != ':' && block[pos] != ';') pos++;
        if (pos >= block.size() || block[pos] != ':') continue;

        P::String name = Css::trim(block.substr(nameStart, pos - nameStart));
        pos++;

        size_t valStart = pos;
        while (pos < block.size() && block[pos] != ';' && block[pos] != '}') pos++;

        P::String value = Css::trim(block.substr(valStart, pos - valStart));
        if (!name.empty() && !value.empty()) props[name] = value;
        if (pos < block.size() && block[pos] == ';') pos++;
    }
    return props;
}

// ============ Parse CSS ============

void Css::parse(std::string_view css) {
    size_t pos = 0;
    while (pos < css.size()) {
        while (pos < css.size() && std::isspace(static_cast<unsigned char>(css[pos]))) pos++;

        // Skip /* ... */ comments
        if (pos + 1 < css.size() && css[pos] == '/' && css[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < css.size() && !(css[pos] == '*' && css[pos + 1] == '/')) pos++;
            pos += 2;
            continue;
        }
        if (pos >= css.size()) break;

        // Selector
        size_t selStart = pos;
        while (pos < css.size() && css[pos] != '{') pos++;
        if (pos >= css.size()) break;
        P::String selector = trim(css.substr(selStart, pos - selStart));
        pos++;

        // Properties block
        size_t propsStart = pos;
        int braceCount = 1;
        while (pos < css.size() && braceCount > 0) {
            if (css[pos] == '{') braceCount++;
            else if (css[pos] == '}') braceCount--;
            pos++;
        }

        std::string_view propsBlock = css.substr(propsStart, pos - propsStart - 1);
        auto props = parseProperties(propsBlock);
        if (props.empty() || selector.empty()) continue;

        Rule rule;
        parseSelectorStr(selector, rule.type, rule.tag, rule.className, rule.id);
        rule.properties = std::move(props);

        LOG_D(Log::UI, "CSS '%s' { %d props }", selector.c_str(), (int)rule.properties.size());
        m_rules.push_back(std::move(rule));
    }
}

// ============ Matching ============

static bool hasClass(const char* classNames, const P::String& cls) {
    if (!classNames || !classNames[0] || cls.empty()) return false;
    const char* p = classNames;
    while (*p) {
        while (*p && std::isspace((unsigned char)*p)) p++;
        if (!*p) break;
        const char* start = p;
        while (*p && !std::isspace((unsigned char)*p)) p++;
        if ((size_t)(p - start) == cls.size() && memcmp(start, cls.c_str(), cls.size()) == 0)
            return true;
    }
    return false;
}

bool Css::matches(const Rule& rule, const char* tag, const char* id, const char* classNames) const {
    switch (rule.type) {
        case SelectorType::TAG:
            return tag && tag[0] && rule.tag == tag;
        case SelectorType::CLASS:
            return hasClass(classNames, rule.className);
        case SelectorType::TAG_CLASS:
            return tag && tag[0] && rule.tag == tag && hasClass(classNames, rule.className);
        case SelectorType::ID:
            return id && id[0] && rule.id == id;
    }
    return false;
}

// ============ Text vertical alignment via padding-top ============

static void applyTextValignCSS(lv_obj_t* obj, const P::String& valign) {
    if (!obj || valign.empty() || valign == "top") return;
    int32_t objH = lv_obj_get_height(obj);
    if (objH <= 0) return;
    const lv_font_t* f = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
    if (!f) f = lv_font_default();
    int lineH = lv_font_get_line_height(f);
    int padBottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
    int available = objH - padBottom;
    if (valign == "center") {
        int pad = (available - lineH) / 2;
        if (pad > 0) lv_obj_set_style_pad_top(obj, pad, 0);
    } else if (valign == "bottom") {
        int pad = available - lineH;
        if (pad > 0) lv_obj_set_style_pad_top(obj, pad, 0);
    }
}

// ============ Apply properties to widget ============

void Css::applyProps(Widget& w, const P::Map<P::String, P::String>& props) const {
    // Deferred text-valign — needs height to be set first
    P::String deferredTextValign;
    P::String deferredTextAlignV;  // vertical part of compound "center center"

    for (const auto& [name, value] : props) {
        if (name == "color") {
            w.applyColor(parseColorValue(value));
        } else if (name == "background" || name == "background-color" || name == "bgcolor") {
            w.applyBgColor(parseColorValue(value));
        } else if (name == "font-size" || name == "font") {
            w.applyFont(parseSizeValue(value));
        } else if (name == "border-radius" || name == "radius") {
            w.applyRadius(parseSizeValue(value));
        } else if (name == "z-index") {
            if (!w.handle) continue;
            setElementZIndex(w.handle, atoi(value.c_str()));
        } else if (name == "width" || name == "w") {
            int v = parse_coord_w(value.c_str()); if (v > 0) lv_obj_set_width(w.handle, v);
        } else if (name == "height" || name == "h") {
            int v = parse_coord_h(value.c_str()); if (v > 0) lv_obj_set_height(w.handle, v);
        } else if (name == "left") {
            int v = parse_coord_w(value.c_str()); if (v >= 0) lv_obj_set_x(w.handle, v);
        } else if (name == "top") {
            int v = parse_coord_h(value.c_str()); if (v >= 0) lv_obj_set_y(w.handle, v);
        } else if (name == "padding") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_all(w.handle, v, 0);
        } else if (name == "padding-left") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_left(w.handle, v, 0);
        } else if (name == "padding-right") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_right(w.handle, v, 0);
        } else if (name == "padding-top") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_top(w.handle, v, 0);
        } else if (name == "padding-bottom") {
            int v = parseSizeValue(value); if (v >= 0) lv_obj_set_style_pad_bottom(w.handle, v, 0);
        } else if (name == "opacity") {
            int opa = (int)(std::atof(value.c_str()) * 255);
            if (opa < 0) opa = 0; if (opa > 255) opa = 255;
            lv_obj_set_style_opa(w.handle, opa, 0);
        } else if (name == "text-align") {
            P::String val(value.data(), value.size());
            auto space = val.find(' ');
            P::String hPart = (space != P::String::npos) ? val.substr(0, space) : val;
            if (hPart == "center")     lv_obj_set_style_text_align(w.handle, LV_TEXT_ALIGN_CENTER, 0);
            else if (hPart == "right") lv_obj_set_style_text_align(w.handle, LV_TEXT_ALIGN_RIGHT, 0);
            else                       lv_obj_set_style_text_align(w.handle, LV_TEXT_ALIGN_LEFT, 0);
            if (space != P::String::npos)
                deferredTextAlignV = val.substr(space + 1);
        } else if (name == "text-valign") {
            deferredTextValign.assign(value.data(), value.size());
        }
    }

    // Apply text-valign AFTER height/font are set
    if (!deferredTextValign.empty())
        applyTextValignCSS(w.handle, deferredTextValign);
    else if (!deferredTextAlignV.empty())
        applyTextValignCSS(w.handle, deferredTextAlignV);
}

// ============ Main cascade method ============

void Css::applyMatching(Widget& w, const char* tag, const char* id, const char* classNames) const {
    if (!w.handle) return;

    // Apply in specificity order: TAG(0) < CLASS(1) < TAG_CLASS(2) < ID(3)
    for (int spec = 0; spec <= 3; spec++) {
        SelectorType st = static_cast<SelectorType>(spec);
        for (const auto& rule : m_rules) {
            if (rule.type == st && matches(rule, tag, id, classNames)) {
                applyProps(w, rule.properties);
            }
        }
    }
}

// ============ Legacy class-only API ============

const Css::Rule* Css::findClass(const P::String& className) const {
    for (const auto& rule : m_rules) {
        if (rule.type == SelectorType::CLASS && rule.className == className)
            return &rule;
    }
    return nullptr;
}

bool Css::has(const P::String& className) const {
    return findClass(className) != nullptr;
}

const P::Map<P::String, P::String>& Css::get(const P::String& className) const {
    auto* r = findClass(className);
    return r ? r->properties : s_empty;
}

const P::String& Css::prop(const P::String& className, const P::String& property) const {
    const auto& props = get(className);
    auto it = props.find(property);
    return it != props.end() ? it->second : s_emptyStr;
}

int Css::getInt(const P::String& className, const P::String& property, int def) const {
    auto val = prop(className, property);
    if (val.empty()) return def;
    return parseSizeValue(val);
}

uint32_t Css::getColor(const P::String& className, const P::String& property, uint32_t def) const {
    auto val = prop(className, property);
    if (val.empty()) return def;
    return parseColorValue(val);
}

// ============ Color/size parsing ============

uint32_t Css::parseColorValue(std::string_view color) {
    if (color.empty()) return 0x000000;
    while (!color.empty() && std::isspace(static_cast<unsigned char>(color.front()))) color = color.substr(1);
    while (!color.empty() && std::isspace(static_cast<unsigned char>(color.back()))) color = color.substr(0, color.size() - 1);
    if (!color.empty() && color[0] == '#') color = color.substr(1);

    if (color == "black") return 0x000000;
    if (color == "white") return 0xFFFFFF;
    if (color == "red") return 0xFF0000;
    if (color == "green") return 0x00FF00;
    if (color == "blue") return 0x0000FF;
    if (color == "yellow") return 0xFFFF00;
    if (color == "cyan") return 0x00FFFF;
    if (color == "magenta") return 0xFF00FF;
    if (color == "gray" || color == "grey") return 0x808080;
    if (color == "orange") return 0xFFA500;
    if (color == "transparent") return 0x000000;

    char buf[8];
    if (color.size() == 6) {
        memcpy(buf, color.data(), 6); buf[6] = '\0';
        return static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
    }
    if (color.size() == 3) {
        buf[1] = '\0';
        buf[0] = color[0]; uint32_t r = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        buf[0] = color[1]; uint32_t g = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        buf[0] = color[2]; uint32_t b = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        return (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
    }
    return 0x000000;
}

int Css::parseSizeValue(std::string_view size) {
    if (size.empty()) return 0;
    char buf[16];
    size_t i = 0;
    for (char c : size) {
        if (i >= 15) break;
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') buf[i++] = c;
        else if (c != ' ') break;
    }
    buf[i] = '\0';
    return i == 0 ? 0 : std::atoi(buf);
}

void Css::dump() const {
    LOG_D(Log::UI, "=== CSS Rules (%d) ===", (int)m_rules.size());
    for (const auto& rule : m_rules) {
        P::String sel;
        switch (rule.type) {
            case SelectorType::TAG:       sel = rule.tag; break;
            case SelectorType::CLASS:     sel = P::String(".") + rule.className; break;
            case SelectorType::TAG_CLASS: sel = rule.tag + P::String(".") + rule.className; break;
            case SelectorType::ID:        sel = P::String("#") + rule.id; break;
        }
        LOG_D(Log::UI, "%s {", sel.c_str());
        for (const auto& [name, value] : rule.properties) {
            LOG_D(Log::UI, "  %s: %s;", name.c_str(), value.c_str());
        }
        LOG_D(Log::UI, "}");
    }
}

} // namespace UI
