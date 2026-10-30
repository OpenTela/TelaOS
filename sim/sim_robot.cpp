// sim_robot.cpp — robot finger: script runner, selectors, checks, tree dump.
#include "sim_robot.h"

#include "lvgl.h"
// Private headers (test tooling only): object boxes, class names, button-matrix key areas.
// Per-module paths exist in LVGL 9.2 and 9.4 alike.
#include "src/core/lv_obj_class_private.h"
#include "src/core/lv_obj_private.h"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#if LV_USE_SPAN
#include "src/widgets/span/lv_span_private.h"
#endif

#ifndef LV_USE_OBJ_NAME
#define LV_USE_OBJ_NAME 0  // LVGL < 9.3
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Sim {
namespace {

constexpr uint32_t kTapHoldMs = 80;  // longer than one LVGL input read period (33 ms)
constexpr uint32_t kSettleMs = 60;   // let LVGL see the release and fire CLICKED

std::vector<std::string> splitCommands(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        if (c == ';' && !quoted) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

// Splits by spaces; a "quoted string" stays one token and keeps its quotes
// (quotes mark a text selector).
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) break;
        size_t j = i;
        if (s[i] == '"') {
            j = s.find('"', i + 1);
            j = (j == std::string::npos) ? s.size() : j + 1;
        } else {
            while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
        }
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

std::string jsonEscape(const char* s) {
    std::string out = "\"";
    for (; s && *s; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
        } else {
            out += static_cast<char>(c);
        }
    }
    return out + "\"";
}

lv_obj_t* const* layers() {
    // Same order LVGL uses to find the pressed object: sys, top, active screen.
    static lv_obj_t* roots[3];
    roots[0] = lv_layer_sys();
    roots[1] = lv_layer_top();
    roots[2] = lv_screen_active();
    return roots;
}

lv_obj_t* findByObjName(const std::string& name) {
#if LV_USE_OBJ_NAME
    lv_obj_t* const* roots = layers();
    for (int i = 0; i < 3; ++i) {
        if (!roots[i]) continue;
        if (lv_obj_t* o = lv_obj_find_by_name(roots[i], name.c_str())) return o;
    }
#else
    (void)name;
#endif
    return nullptr;
}

const char* objName(const lv_obj_t* o) {
#if LV_USE_OBJ_NAME
    return lv_obj_get_name(o);
#else
    (void)o;
    return nullptr;
#endif
}

lv_obj_t* hitTest(int x, int y) {
    lv_point_t pt{x, y};
    lv_obj_t* const* roots = layers();
    for (int i = 0; i < 3; ++i) {
        if (!roots[i]) continue;
        if (lv_obj_t* o = lv_indev_search_obj(roots[i], &pt)) return o;
    }
    return nullptr;
}

bool isAncestor(const lv_obj_t* anc, const lv_obj_t* o) {
    for (; o; o = lv_obj_get_parent(o)) {
        if (o == anc) return true;
    }
    return false;
}

std::string describe(const lv_obj_t* o) {
    if (!o) return "nothing";
    const lv_obj_class_t* cls = lv_obj_get_class(o);
    std::string s = (cls && cls->name) ? cls->name : "obj";
    if (const char* n = objName(o)) s += std::string(" @") + n;
    return s;
}

// Text of a spangroup: all its spans in order
std::string spanText(lv_obj_t* o) {
    std::string s;
#if LV_USE_SPAN
    const uint32_t n = lv_spangroup_get_span_count(o);
    for (uint32_t i = 0; i < n; ++i) {
        lv_span_t* sp = lv_spangroup_get_child(o, static_cast<int32_t>(i));
        if (sp && sp->txt) s += sp->txt;
    }
#else
    (void)o;
#endif
    return s;
}

bool isSpangroup(const lv_obj_t* o) {
#if LV_USE_SPAN
    return lv_obj_check_type(o, &lv_spangroup_class);
#else
    (void)o;
    return false;
#endif
}

// Widget value as text. Each widget is guarded: the firmware's lv_conf may disable it.
std::string stateOf(lv_obj_t* o) {
    char buf[128] = {0};
    (void)buf;
    if (lv_obj_check_type(o, &lv_label_class)) return lv_label_get_text(o);
    if (isSpangroup(o)) return spanText(o);
#if LV_USE_SPINBOX
    if (lv_obj_check_type(o, &lv_spinbox_class)) return std::to_string(lv_spinbox_get_value(o));
#endif
#if LV_USE_TEXTAREA
    if (lv_obj_check_type(o, &lv_textarea_class)) return lv_textarea_get_text(o);
#endif
#if LV_USE_SLIDER
    if (lv_obj_check_type(o, &lv_slider_class)) return std::to_string(lv_slider_get_value(o));
#endif
#if LV_USE_BAR
    if (lv_obj_check_type(o, &lv_bar_class)) return std::to_string(lv_bar_get_value(o));
#endif
#if LV_USE_ARC
    if (lv_obj_check_type(o, &lv_arc_class)) return std::to_string(lv_arc_get_value(o));
#endif
#if LV_USE_LED
    if (lv_obj_check_type(o, &lv_led_class)) return std::to_string(lv_led_get_brightness(o));
#endif
#if LV_USE_TABVIEW
    if (lv_obj_check_type(o, &lv_tabview_class)) return std::to_string(lv_tabview_get_tab_active(o));
#endif
#if LV_USE_DROPDOWN
    if (lv_obj_check_type(o, &lv_dropdown_class)) {
        lv_dropdown_get_selected_str(o, buf, sizeof buf);
        return buf;
    }
#endif
#if LV_USE_ROLLER
    if (lv_obj_check_type(o, &lv_roller_class)) {
        lv_roller_get_selected_str(o, buf, sizeof buf);
        return buf;
    }
#endif
    if (lv_obj_check_type(o, &lv_buttonmatrix_class)) {
        auto* bm = reinterpret_cast<lv_buttonmatrix_t*>(o);
        for (uint32_t i = 0; i < bm->btn_cnt; ++i) {
            if (lv_buttonmatrix_has_button_ctrl(o, i, LV_BUTTONMATRIX_CTRL_CHECKED)) {
                return lv_buttonmatrix_get_button_text(o, i);
            }
        }
        return "";
    }
    bool checkable = lv_obj_has_flag(o, LV_OBJ_FLAG_CHECKABLE);
#if LV_USE_SWITCH
    checkable = checkable || lv_obj_check_type(o, &lv_switch_class);
#endif
#if LV_USE_CHECKBOX
    checkable = checkable || lv_obj_check_type(o, &lv_checkbox_class);
#endif
    if (checkable) return lv_obj_has_state(o, LV_STATE_CHECKED) ? "on" : "off";
    return "";
}

// Visible text of a container (HTML innerText, roughly): labels and spangroups in tree order,
// hidden subtrees skipped, pieces joined by one space
void collectText(lv_obj_t* o, std::string& out) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    std::string piece;
    if (lv_obj_check_type(o, &lv_label_class)) piece = lv_label_get_text(o);
    else if (isSpangroup(o)) piece = spanText(o);
    if (!piece.empty()) {
        if (!out.empty() && !std::isspace(static_cast<unsigned char>(out.back())) &&
            !std::isspace(static_cast<unsigned char>(piece.front()))) {
            out += ' ';
        }
        out += piece;
    }
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; ++i) collectText(lv_obj_get_child(o, i), out);
}

std::string textContent(lv_obj_t* o) {
    std::string s;
    collectText(o, s);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool valueMatches(const std::string& actual, const std::string& expected, bool contains = false) {
    if (contains) return actual.find(expected) != std::string::npos;
    // "lo..hi": numeric range. Parse each side separately: strtod("25..35") would eat "25."
    auto number = [](const std::string& s, double& out) {
        char* end = nullptr;
        out = std::strtod(s.c_str(), &end);
        return !s.empty() && end == s.c_str() + s.size();
    };
    const size_t dots = expected.find("..");
    double lo = 0, hi = 0, v = 0;
    if (dots != std::string::npos && number(expected.substr(0, dots), lo) &&
        number(expected.substr(dots + 2), hi) && number(actual, v)) {
        return v >= lo && v <= hi;
    }
    return actual == expected;
}

// A label matches when its whole text equals `text`, or one of its lines does
// (dropdown lists and rollers keep all options in one multi-line label).
bool matchLabel(lv_obj_t* o, const std::string& text, int& x, int& y) {
    const char* s = lv_label_get_text(o);
    if (!s) return false;
    lv_area_t c;
    lv_obj_get_content_coords(o, &c);
    std::string trimmed = s;  // word labels carry their trailing space
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
    if (text == s || text == trimmed) {
        x = (c.x1 + c.x2) / 2;
        y = (c.y1 + c.y2) / 2;
        return true;
    }
    if (!std::strchr(s, '\n')) return false;
    const int32_t line_h = lv_font_get_line_height(lv_obj_get_style_text_font(o, LV_PART_MAIN));
    uint32_t char_id = 0;
    const char* line = s;
    for (;;) {
        const char* end = std::strchr(line, '\n');
        const size_t len = end ? static_cast<size_t>(end - line) : std::strlen(line);
        if (len == text.size() && std::memcmp(line, text.data(), len) == 0) {
            lv_point_t pos;
            lv_label_get_letter_pos(o, char_id, &pos);
            x = (c.x1 + c.x2) / 2;
            y = c.y1 + pos.y + line_h / 2;
            return true;
        }
        if (!end) return false;
        for (size_t k = 0; k < len; ++k) {
            if ((static_cast<unsigned char>(line[k]) & 0xC0) != 0x80) ++char_id;  // UTF-8 chars
        }
        ++char_id;  // '\n'
        line = end + 1;
    }
}

lv_obj_t* findText(lv_obj_t* o, const std::string& text, int& x, int& y) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    // has_class, not check_type: rollers use a label subclass for their options
    if (lv_obj_has_class(o, &lv_label_class) && lv_obj_is_visible(o) && matchLabel(o, text, x, y)) {
        return o;
    }
    // formatted paragraph: whole text, or one of its spans (a link) — tap its centre
    if (isSpangroup(o) && lv_obj_is_visible(o)) {
        bool hit = spanText(o) == text;
#if LV_USE_SPAN
        for (uint32_t i = 0; !hit && i < lv_spangroup_get_span_count(o); ++i) {
            lv_span_t* sp = lv_spangroup_get_child(o, static_cast<int32_t>(i));
            hit = sp && sp->txt && text == sp->txt;
        }
#endif
        if (hit) {
            lv_area_t c;
            lv_obj_get_content_coords(o, &c);
            x = (c.x1 + c.x2) / 2;
            y = (c.y1 + c.y2) / 2;
            return o;
        }
    }
    bool is_matrix = lv_obj_check_type(o, &lv_buttonmatrix_class);
#if LV_USE_KEYBOARD
    is_matrix = is_matrix || lv_obj_check_type(o, &lv_keyboard_class);
#endif
    if (is_matrix && lv_obj_is_visible(o)) {
        auto* bm = reinterpret_cast<lv_buttonmatrix_t*>(o);
        for (uint32_t i = 0; i < bm->btn_cnt; ++i) {
            const char* bt = lv_buttonmatrix_get_button_text(o, i);
            if (!bt || text != bt) continue;
            if (lv_buttonmatrix_has_button_ctrl(o, i, LV_BUTTONMATRIX_CTRL_HIDDEN)) continue;
            const lv_area_t& a = bm->button_areas[i];
            x = o->coords.x1 + (a.x1 + a.x2) / 2;
            y = o->coords.y1 + (a.y1 + a.y2) / 2;
            return o;
        }
    }
    const uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; ++i) {
        if (lv_obj_t* hit = findText(lv_obj_get_child(o, i), text, x, y)) return hit;
    }
    return nullptr;
}

void dumpObj(std::ostream& f, lv_obj_t* o, int depth) {
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const lv_obj_class_t* cls = lv_obj_get_class(o);
    const std::string pad(depth * 2, ' ');
    f << pad << "{\"type\": " << jsonEscape(cls && cls->name ? cls->name : "obj");
    if (const char* n = objName(o)) f << ", \"name\": " << jsonEscape(n);
    f << ", \"x\": " << a.x1 << ", \"y\": " << a.y1 << ", \"w\": " << lv_area_get_width(&a)
      << ", \"h\": " << lv_area_get_height(&a);
    if (!lv_obj_is_visible(o)) f << ", \"visible\": false";
    // Overflow a robot should notice: content taller/wider than the box (shows a scrollbar)
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE)) {
        const int32_t sy = lv_obj_get_scroll_top(o) + lv_obj_get_scroll_bottom(o);
        const int32_t sx = lv_obj_get_scroll_left(o) + lv_obj_get_scroll_right(o);
        if (sy > 0) f << ", \"overflow_y\": " << sy;
        if (sx > 0) f << ", \"overflow_x\": " << sx;
    }
    const std::string v = stateOf(o);
    if (!v.empty()) f << ", \"value\": " << jsonEscape(v.c_str());
    const uint32_t n = lv_obj_get_child_count(o);
    if (n) {
        f << ", \"children\": [\n";
        for (uint32_t i = 0; i < n; ++i) {
            dumpObj(f, lv_obj_get_child(o, i), depth + 1);
            f << (i + 1 < n ? ",\n" : "\n");
        }
        f << pad << "]";
    }
    f << "}";
}

}  // namespace

Robot::Robot(Panel& panel, Advance advance, std::string out_dir, RobotHooks hooks)
    : panel_(panel), advance_(std::move(advance)), out_dir_(std::move(out_dir)), hooks_(std::move(hooks)) {
    std::error_code ec;
    std::filesystem::create_directories(out_dir_, ec);
}

lv_obj_t* Robot::findById(const std::string& id) const {
    if (hooks_.findById) {
        if (lv_obj_t* o = hooks_.findById(id)) return o;
    }
    return findByObjName(id);
}

bool Robot::runFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "[robot] cannot open script %s\n", path.c_str());
        ++failed_;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return runText(ss.str(), std::filesystem::path(path).filename().string());
}

bool Robot::runText(const std::string& text, const std::string& origin) {
    std::istringstream in(text);
    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string where = origin + ":" + std::to_string(line_no);
        // "> line": raw firmware console input, passed through untouched (no ';' split)
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] == '>') {
            const size_t start = line.find_first_not_of(" \t", first + 1);
            const std::string cmd = start == std::string::npos ? "" : line.substr(start);
            if (!hooks_.console) {
                std::printf("[robot %6u] ✗ %s no firmware console in this build\n", lv_tick_get(), where.c_str());
                ++failed_;
                return false;
            }
            std::printf("[robot %6u] > %s\n", lv_tick_get(), cmd.c_str());
            hooks_.console(cmd);
            if (!advance_(20)) return false;  // let the firmware loop read and execute it
            continue;
        }
        for (const std::string& cmd : splitCommands(line)) {
            const std::vector<std::string> a = tokenize(cmd);
            if (a.empty() || a[0][0] == '#') continue;
            if (!exec(a, where)) return false;
            if (quit_) return true;
        }
    }
    return true;
}

bool Robot::resolve(const std::string& t, Point& p, std::string& err) {
    lv_obj_t* target = nullptr;
    if (!t.empty() && t[0] == '@') {
        std::string name = t.substr(1);
        double fx = 0.5, fy = 0.5;
        const size_t colon = name.find(':');
        if (colon != std::string::npos) {
            if (std::sscanf(name.c_str() + colon + 1, "%lf,%lf", &fx, &fy) != 2) {
                err = "bad point in " + t + " (use @name:0.5,0.5)";
                return false;
            }
            name.resize(colon);
        }
        target = findById(name);
        if (!target) {
            err = "no widget @" + name;
            return false;
        }
        lv_obj_update_layout(target);
        lv_area_t a;
        lv_obj_get_coords(target, &a);
        p.x = a.x1 + static_cast<int>(fx * (lv_area_get_width(&a) - 1) + 0.5);
        p.y = a.y1 + static_cast<int>(fy * (lv_area_get_height(&a) - 1) + 0.5);
        if (!lv_obj_is_visible(target)) {
            err = "@" + name + " is not visible (hidden or scrolled away)";
            return false;
        }
    } else if (!t.empty() && t[0] == '"') {
        const std::string text = unquote(t);
        lv_obj_t* const* roots = layers();
        for (int i = 0; i < 3 && !target; ++i) {
            if (roots[i]) target = findText(roots[i], text, p.x, p.y);
        }
        if (!target) {
            err = "no visible text " + t;
            return false;
        }
    } else {
        if (std::sscanf(t.c_str(), "%d,%d", &p.x, &p.y) != 2) {
            err = "bad target '" + t + "' (use @name, \"text\" or x,y)";
            return false;
        }
        return true;  // raw coordinates: no actionability check, like a real finger
    }

    if (p.x < 0 || p.y < 0 || p.x >= panel_.width() || p.y >= panel_.height()) {
        err = t + " is off-screen at (" + std::to_string(p.x) + "," + std::to_string(p.y) + ")";
        return false;
    }
    // Actionability: the finger must land on the target (or its child/parent), not on
    // something that covers it.
    lv_obj_t* hit = hitTest(p.x, p.y);
    if (!hit || !(isAncestor(hit, target) || isAncestor(target, hit))) {
        err = t + " at (" + std::to_string(p.x) + "," + std::to_string(p.y) + ") is covered by " +
              describe(hit);
        return false;
    }
    return true;
}

bool Robot::tap(const Point& p, uint32_t hold_ms) {
    panel_.touch(p.x, p.y, true);
    if (!advance_(hold_ms)) return false;
    panel_.touch(p.x, p.y, false);
    return advance_(kSettleMs);
}

bool Robot::drag(const Point& from, const Point& to, uint32_t ms) {
    panel_.touch(from.x, from.y, true);
    if (!advance_(40)) return false;
    const uint32_t steps = std::max<uint32_t>(1, ms / 10);
    for (uint32_t i = 1; i <= steps; ++i) {
        const int x = from.x + static_cast<int>((to.x - from.x) * static_cast<int64_t>(i) / steps);
        const int y = from.y + static_cast<int>((to.y - from.y) * static_cast<int64_t>(i) / steps);
        panel_.touch(x, y, true);
        if (!advance_(std::max<uint32_t>(1, ms / steps))) return false;
    }
    if (!advance_(40)) return false;  // rest before lifting: no fling
    panel_.touch(to.x, to.y, false);
    return advance_(kSettleMs);
}

bool Robot::saveTree(const std::string& path) {
    lv_refr_now(nullptr);
    std::ofstream f(path);
    if (!f) return false;
    f << "{\"time_ms\": " << lv_tick_get() << ",\n\"screen\":\n";
    dumpObj(f, lv_screen_active(), 0);
    f << ",\n\"top\":\n";
    dumpObj(f, lv_layer_top(), 0);
    f << "\n}\n";
    return true;
}

bool Robot::exec(const std::vector<std::string>& a, const std::string& where) {
    const std::string& cmd = a[0];
    const unsigned t = lv_tick_get();
    auto fail = [&](const std::string& msg) {
        std::printf("[robot %6u] ✗ %s %s\n", t, where.c_str(), msg.c_str());
        ++failed_;
        return false;
    };
    auto argMs = [&](size_t i, uint32_t def) {
        return i < a.size() ? static_cast<uint32_t>(std::strtoul(a[i].c_str(), nullptr, 10)) : def;
    };
    auto need = [&](size_t n) { return a.size() >= n; };
    std::string err;
    Point p, q;

    if (cmd == "wait") {
        if (!need(2)) return fail("wait <ms>");
        return advance_(argMs(1, 0));
    }
    if (cmd == "tap" || cmd == "longpress" || cmd == "press" || cmd == "move") {
        if (!need(2)) return fail(cmd + " <target>");
        if (!resolve(a[1], p, err)) return fail(cmd + ": " + err);
        std::printf("[robot %6u] %s %s → (%d,%d)\n", t, cmd.c_str(), a[1].c_str(), p.x, p.y);
        if (cmd == "tap") return tap(p, kTapHoldMs);
        if (cmd == "longpress") return tap(p, argMs(2, 800));
        panel_.touch(p.x, p.y, cmd == "press" ? true : panel_.touchPressed());
        return advance_(1);
    }
    if (cmd == "release") {
        panel_.touch(panel_.touchX(), panel_.touchY(), false);
        return advance_(kSettleMs);
    }
    if (cmd == "drag") {
        if (!need(3)) return fail("drag <from> <to> [ms]");
        if (!resolve(a[1], p, err) || !resolve(a[2], q, err)) return fail("drag: " + err);
        std::printf("[robot %6u] drag (%d,%d) → (%d,%d)\n", t, p.x, p.y, q.x, q.y);
        return drag(p, q, argMs(3, 300));
    }
    // expect <@id|"text"> :visible | :hidden
    if (cmd == "expect" && need(3) && (a[2] == ":visible" || a[2] == ":hidden")) {
        lv_obj_t* o = nullptr;
        if (a[1][0] == '@') {
            o = findById(a[1].substr(1));
        } else {
            int x = 0, y = 0;
            lv_obj_t* const* roots = layers();
            for (int i = 0; i < 3 && !o; ++i) {
                if (roots[i]) o = findText(roots[i], unquote(a[1]), x, y);
            }
        }
        const bool visible = o && lv_obj_is_visible(o);
        const bool want = a[2] == ":visible";
        if (visible == want) {
            ++passed_;
            std::printf("[robot %6u] ✓ %s %s\n", t, a[1].c_str(), a[2].c_str());
        } else {
            fail(a[1] + (o ? (visible ? " is visible" : " is hidden") : " not found") + ", expected " + a[2]);
        }
        return true;
    }
    // expect <target> [contains] <value>
    const bool contains = cmd == "expect" && a.size() >= 4 && a[2] == "contains";
    if (cmd == "expect" && need(3) && a[1][0] == '$') {
        const std::string expected = unquote(a[contains ? 3 : 2]);
        std::string actual;
        if (!hooks_.stateVar) return fail("no app state in this build");
        if (!hooks_.stateVar(a[1].substr(1), actual)) {
            fail(a[1] + " is not defined in app state");
            return true;
        }
        if (valueMatches(actual, expected, contains)) {
            ++passed_;
            std::printf("[robot %6u] ✓ %s = \"%s\"\n", t, a[1].c_str(), actual.c_str());
        } else {
            fail(a[1] + " = \"" + actual + "\", expected \"" + expected + "\"");
        }
        return true;
    }
    if (cmd == "scroll" || cmd == "text" || cmd == "expect") {
        if (!need(cmd == "scroll" ? 2 : 3) || a[1][0] != '@') return fail(cmd + " @id ...");
        lv_obj_t* o = findById(a[1].substr(1));
        if (!o) return fail("no widget " + a[1]);
        if (cmd == "scroll") {
            lv_obj_scroll_to_view_recursive(o, LV_ANIM_OFF);
            return advance_(1);
        }
        const std::string value = unquote(a[2]);
        if (cmd == "text") {
            if (lv_obj_check_type(o, &lv_textarea_class)) {
                lv_textarea_set_text(o, value.c_str());
            } else if (lv_obj_check_type(o, &lv_label_class)) {
                lv_label_set_text(o, value.c_str());
            } else {
                return fail("text: " + describe(o) + " has no text");
            }
            std::printf("[robot %6u] text %s = \"%s\"\n", t, a[1].c_str(), value.c_str());
            return advance_(1);
        }
        std::string actual = stateOf(o);
        // a container (an HTML block, a TelaOS div) is checked by the text it shows
        if (actual.empty() && lv_obj_get_child_count(o) > 0 && !lv_obj_check_type(o, &lv_textarea_class)) {
            actual = textContent(o);
        }
        const std::string expected = contains ? unquote(a[3]) : value;
        if (valueMatches(actual, expected, contains)) {
            ++passed_;
            std::printf("[robot %6u] ✓ %s = \"%s\"\n", t, a[1].c_str(), actual.c_str());
        } else {
            fail(a[1] + " = \"" + actual + "\", expected " + (contains ? "to contain " : "") + "\"" +
                 expected + "\"");
        }
        return true;  // a failed check does not stop the script
    }
    if (cmd == "shot" || cmd == "tree") {
        if (!need(2)) return fail(cmd + " <name>");
        const std::string path = out_dir_ + "/" + a[1] + (cmd == "shot" ? ".png" : ".json");
        bool ok;
        if (cmd == "shot") {
            lv_refr_now(nullptr);  // render pending changes first: the shot is what the panel shows now
            ok = panel_.savePng(path);
        } else {
            ok = saveTree(path);
        }
        if (!ok) return fail("cannot write " + path);
        std::printf("[robot %6u] %s %s\n", t, cmd.c_str(), path.c_str());
        return true;
    }
    if (cmd == "log") {
        std::string msg;
        for (size_t i = 1; i < a.size(); ++i) msg += (i > 1 ? " " : "") + unquote(a[i]);
        std::printf("[robot %6u] %s\n", t, msg.c_str());
        return true;
    }
    if (cmd == "quit") {
        quit_ = true;
        return true;
    }
    return fail("unknown command '" + cmd + "'");
}

}  // namespace Sim
