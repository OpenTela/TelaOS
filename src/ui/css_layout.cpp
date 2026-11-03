/**
 * css_layout.cpp — see css_layout.h
 *
 * LVGL's flex is CSS flexbox without stretch and order. position: relative is a translation;
 * absolute and fixed take the object out of its parent's layout (fixed out of its scrolling too)
 * and place it by its parent's edges.
 */
#include "ui/css_layout.h"
#include "ui/css_values.h"
#include "ui_layout.h"  // SCREEN_WIDTH, SCREEN_HEIGHT

#include <algorithm>
#include <array>
#include <cmath>

namespace UI {

namespace {

constexpr char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
constexpr bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

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

// ============ Flex container ============

struct Flow {
    bool column = false;
    bool wrap = false;
    bool reverse = false;
};

Flow flowOf(lv_flex_flow_t f) {
    switch (f) {
        case LV_FLEX_FLOW_COLUMN: return {true, false, false};
        case LV_FLEX_FLOW_ROW_WRAP: return {false, true, false};
        case LV_FLEX_FLOW_COLUMN_WRAP: return {true, true, false};
        case LV_FLEX_FLOW_ROW_REVERSE: return {false, false, true};
        case LV_FLEX_FLOW_COLUMN_REVERSE: return {true, false, true};
        case LV_FLEX_FLOW_ROW_WRAP_REVERSE: return {false, true, true};
        case LV_FLEX_FLOW_COLUMN_WRAP_REVERSE: return {true, true, true};
        default: return {};
    }
}

lv_flex_flow_t lvFlow(const Flow& f) {
    if (f.column) {
        if (f.wrap) return f.reverse ? LV_FLEX_FLOW_COLUMN_WRAP_REVERSE : LV_FLEX_FLOW_COLUMN_WRAP;
        return f.reverse ? LV_FLEX_FLOW_COLUMN_REVERSE : LV_FLEX_FLOW_COLUMN;
    }
    if (f.wrap) return f.reverse ? LV_FLEX_FLOW_ROW_WRAP_REVERSE : LV_FLEX_FLOW_ROW_WRAP;
    return f.reverse ? LV_FLEX_FLOW_ROW_REVERSE : LV_FLEX_FLOW_ROW;
}

// flex-direction: row, row-reverse, column, column-reverse
bool direction(std::string_view v, Flow& f) {
    v = trimmed(v);
    if (equalsNoCase(v, "row")) f.column = false, f.reverse = false;
    else if (equalsNoCase(v, "row-reverse")) f.column = false, f.reverse = true;
    else if (equalsNoCase(v, "column")) f.column = true, f.reverse = false;
    else if (equalsNoCase(v, "column-reverse")) f.column = true, f.reverse = true;
    else return false;
    return true;
}

// flex-wrap: nowrap, wrap; wrap-reverse wraps (LVGL wraps the lines one way only)
bool wrapping(std::string_view v, bool& wrap) {
    v = trimmed(v);
    if (equalsNoCase(v, "nowrap")) wrap = false;
    else if (equalsNoCase(v, "wrap") || equalsNoCase(v, "wrap-reverse")) wrap = true;
    else return false;
    return true;
}

// justify-content, align-content
bool contentPlace(std::string_view v, lv_flex_align_t& a) {
    v = trimmed(v);
    if (equalsNoCase(v, "flex-start") || equalsNoCase(v, "start") || equalsNoCase(v, "left") ||
        equalsNoCase(v, "normal") || equalsNoCase(v, "stretch")) {
        a = LV_FLEX_ALIGN_START;
    } else if (equalsNoCase(v, "flex-end") || equalsNoCase(v, "end") || equalsNoCase(v, "right")) {
        a = LV_FLEX_ALIGN_END;
    } else if (equalsNoCase(v, "center")) {
        a = LV_FLEX_ALIGN_CENTER;
    } else if (equalsNoCase(v, "space-between")) {
        a = LV_FLEX_ALIGN_SPACE_BETWEEN;
    } else if (equalsNoCase(v, "space-around")) {
        a = LV_FLEX_ALIGN_SPACE_AROUND;
    } else if (equalsNoCase(v, "space-evenly")) {
        a = LV_FLEX_ALIGN_SPACE_EVENLY;
    } else {
        return false;
    }
    return true;
}

// align-items; stretch (the CSS default) and baseline are not in LVGL: the items stay at the start
bool itemsPlace(std::string_view v, lv_flex_align_t& a) {
    v = trimmed(v);
    if (equalsNoCase(v, "flex-start") || equalsNoCase(v, "start") || equalsNoCase(v, "self-start") ||
        equalsNoCase(v, "normal") || equalsNoCase(v, "stretch") || equalsNoCase(v, "baseline")) {
        a = LV_FLEX_ALIGN_START;
    } else if (equalsNoCase(v, "flex-end") || equalsNoCase(v, "end") || equalsNoCase(v, "self-end")) {
        a = LV_FLEX_ALIGN_END;
    } else if (equalsNoCase(v, "center")) {
        a = LV_FLEX_ALIGN_CENTER;
    } else {
        return false;
    }
    return true;
}

// ============ Flex item ============

int growOf(float g) { return g > 0 ? std::min(255, std::max(1, static_cast<int>(std::lround(g)))) : 0; }

// flex-grow: 2; flex: 1, flex: 2 1 0, flex: 100px (that is 1 1 100px), auto, none
bool growValue(std::string_view name, std::string_view v, int& grow) {
    v = trimmed(v);
    size_t i = 0;
    float g = 0;
    if (name == "flex-grow") {
        if (!cssNumber(v, i, g) || i != v.size() || g < 0) return false;
        grow = growOf(g);
        return true;
    }
    if (equalsNoCase(v, "none") || equalsNoCase(v, "initial")) {
        grow = 0;
        return true;
    }
    if (equalsNoCase(v, "auto")) {
        grow = 1;
        return true;
    }
    const P::Array<std::string_view> parts = cssParts(v);
    if (parts.empty() || parts.size() > 3) return false;
    if (cssNumber(parts[0], i, g) && i == parts[0].size()) {  // flex: <grow> [<shrink>] [<basis>]
        if (g < 0) return false;
        grow = growOf(g);
        return true;
    }
    int32_t px = 0;
    float pct = 0;
    if (parts.size() == 1 && (cssLength(parts[0], kCssRemPx, px) || cssPercent(parts[0], pct))) {
        grow = 1;  // flex: <basis>
        return true;
    }
    return false;
}

// ============ Position ============

enum class Position { Static, Relative, Absolute, Fixed };

bool positionValue(std::string_view v, Position& p) {
    v = trimmed(v);
    if (equalsNoCase(v, "static") || equalsNoCase(v, "sticky")) p = Position::Static;  // LVGL has no sticky
    else if (equalsNoCase(v, "relative")) p = Position::Relative;
    else if (equalsNoCase(v, "absolute")) p = Position::Absolute;
    else if (equalsNoCase(v, "fixed")) p = Position::Fixed;
    else return false;
    return true;
}

enum Edge { kLeft, kTop, kRight, kBottom };
constexpr std::array<std::string_view, 4> kEdges{"left", "top", "right", "bottom"};

}  // namespace

void applyCssLayout(lv_obj_t* o, const P::Array<CssDecl>& decls, float em) {
    if (!o) return;
    bool display = false;  // display: flex or inline-flex
    Flow flow;
    bool hasDir = false, hasWrap = false;
    lv_flex_align_t mainPlace = LV_FLEX_ALIGN_START, crossPlace = LV_FLEX_ALIGN_START, trackPlace = LV_FLEX_ALIGN_START;
    bool hasMain = false, hasCross = false, hasTrack = false;
    int grow = 0;
    bool hasGrow = false;
    Position position = Position::Static;
    std::array<int32_t, 4> edge{};
    std::array<bool, 4> hasEdge{};
    bool hidden = false;
    for (const CssDecl& d : decls) {
        const std::string_view name = d.name, value = d.value;
        lv_flex_align_t a = LV_FLEX_ALIGN_START;
        if (name == "display") {
            const std::string_view v = trimmed(value);
            display = equalsNoCase(v, "flex") || equalsNoCase(v, "inline-flex");  // none: applyProps
        } else if (name == "flex-direction") {
            if (direction(value, flow)) hasDir = true;
        } else if (name == "flex-wrap") {
            if (wrapping(value, flow.wrap)) hasWrap = true;
        } else if (name == "flex-flow") {  // flex-flow: column wrap — each part at most once
            Flow f = flow;
            bool dir = false, wr = false, ok = true;
            const P::Array<std::string_view> parts = cssParts(value);
            for (std::string_view p : parts) {
                if (!dir && direction(p, f)) dir = true;
                else if (!wr && wrapping(p, f.wrap)) wr = true;
                else ok = false;
            }
            if (ok && !parts.empty()) {
                flow = f;
                hasDir = hasDir || dir;
                hasWrap = hasWrap || wr;
            }
        } else if (name == "justify-content") {
            if (contentPlace(value, a)) mainPlace = a, hasMain = true;
        } else if (name == "align-items") {
            if (itemsPlace(value, a)) crossPlace = a, hasCross = true;
        } else if (name == "align-content") {
            if (contentPlace(value, a)) trackPlace = a, hasTrack = true;
        } else if (name == "flex-grow" || name == "flex") {
            if (growValue(name, value, grow)) hasGrow = true;
        } else if (name == "position") {
            positionValue(value, position);
        } else if (name == "visibility") {
            const std::string_view v = trimmed(value);
            if (equalsNoCase(v, "hidden") || equalsNoCase(v, "collapse")) hidden = true;
            else if (equalsNoCase(v, "visible")) hidden = false;
        } else {
            for (size_t k = 0; k < kEdges.size(); ++k) {
                if (name != kEdges[k]) continue;
                const int32_t screen = (k == kLeft || k == kRight) ? SCREEN_WIDTH : SCREEN_HEIGHT;
                if (cssCoord(value, em, screen, edge[k])) hasEdge[k] = true;
            }
        }
    }

    // A flex container: display: flex here, or one already (<div flex>, a block of the HTML flow)
    const bool isFlex = display || lv_obj_get_style_layout(o, LV_PART_MAIN) == LV_LAYOUT_FLEX;
    Flow f = display ? Flow{} : flowOf(lv_obj_get_style_flex_flow(o, LV_PART_MAIN));  // display: flex is a row
    if (hasDir) f.column = flow.column, f.reverse = flow.reverse;
    if (hasWrap) f.wrap = flow.wrap;
    if (isFlex && (display || hasDir || hasWrap)) lv_obj_set_flex_flow(o, lvFlow(f));
    // In CSS a single line fills the container across, so align-items places the items in the
    // container; an LVGL track is as wide as its widest item, so the track goes there too
    if (hasCross && !f.wrap && !hasTrack) {
        trackPlace = crossPlace;
        hasTrack = true;
    }
    if (isFlex && (hasMain || hasCross || hasTrack)) {
        lv_obj_set_flex_align(o, hasMain ? mainPlace : lv_obj_get_style_flex_main_place(o, LV_PART_MAIN),
                              hasCross ? crossPlace : lv_obj_get_style_flex_cross_place(o, LV_PART_MAIN),
                              hasTrack ? trackPlace : lv_obj_get_style_flex_track_place(o, LV_PART_MAIN));
    }

    // A flex item grows into the room its container has along the main axis. A container sized by
    // its content has none, as in CSS, and LVGL would loop on it (grow is measured from it).
    if (hasGrow) {
        lv_obj_t* parent = lv_obj_get_parent(o);
        if (parent && lv_obj_get_style_layout(parent, LV_PART_MAIN) == LV_LAYOUT_FLEX) {
            const bool column = flowOf(lv_obj_get_style_flex_flow(parent, LV_PART_MAIN)).column;
            const int32_t size = column ? lv_obj_get_style_height(parent, LV_PART_MAIN)
                                        : lv_obj_get_style_width(parent, LV_PART_MAIN);
            if (grow == 0 || size != LV_SIZE_CONTENT) lv_obj_set_flex_grow(o, static_cast<uint8_t>(grow));
        }
    }

    switch (position) {
        case Position::Static:  // TelaOS: left and top place the widget, as x= and y= do
            if (hasEdge[kLeft]) lv_obj_set_x(o, edge[kLeft]);
            if (hasEdge[kTop]) lv_obj_set_y(o, edge[kTop]);
            break;
        case Position::Relative:  // moved from its place in the layout; left wins over right, top over bottom
            if (hasEdge[kLeft] || hasEdge[kRight]) {
                lv_obj_set_style_translate_x(o, hasEdge[kLeft] ? edge[kLeft] : -edge[kRight], 0);
            }
            if (hasEdge[kTop] || hasEdge[kBottom]) {
                lv_obj_set_style_translate_y(o, hasEdge[kTop] ? edge[kTop] : -edge[kBottom], 0);
            }
            break;
        case Position::Absolute:
        case Position::Fixed: {
            lv_obj_add_flag(o, position == Position::Fixed ? LV_OBJ_FLAG_FLOATING : LV_OBJ_FLAG_IGNORE_LAYOUT);
            if (!hasEdge[kLeft] && !hasEdge[kTop] && !hasEdge[kRight] && !hasEdge[kBottom]) break;
            const bool fromRight = hasEdge[kRight] && !hasEdge[kLeft];
            const bool fromBottom = hasEdge[kBottom] && !hasEdge[kTop];
            const lv_align_t align = fromBottom ? (fromRight ? LV_ALIGN_BOTTOM_RIGHT : LV_ALIGN_BOTTOM_LEFT)
                                                : (fromRight ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT);
            lv_obj_align(o, align, fromRight ? -edge[kRight] : edge[kLeft], fromBottom ? -edge[kBottom] : edge[kTop]);
            break;
        }
    }

    if (hidden) {  // visibility: hidden keeps its place in the layout; it is not seen and takes no taps
        lv_obj_set_style_opa(o, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    }
}

}  // namespace UI
