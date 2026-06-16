/**
 * LVGL Mock - captures widget tree with styles
 * Supports hierarchical queries
 */
#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>

namespace LvglMock {

struct Widget {
    std::string type;
    std::string id;
    int x = 0, y = 0, w = 0, h = 0;
    std::string text;
    bool hidden = false;
    void* lv_obj = nullptr;  // back-pointer to lv_obj_t (void* to avoid circular dep)
    
    // Styles
    uint32_t color = 0;      // text color
    uint32_t bgcolor = 0;    // background color
    int fontSize = 0;
    int radius = 0;
    int opacity = 255;       // 0-255
    int padAll = -1;         // padding (from lv_obj_set_style_pad_all)
    int padLeft = 0;
    int padRight = 0;
    int padBottom = 0;
    bool hasColor = false;
    bool hasBgcolor = false;
    
    // Text alignment (inside widget)
    int textAlignH = 0;      // 0=left, 1=center, 2=right
    int padTop = 0;          // for vertical text alignment
    
    // Element alignment (position on parent)
    int alignType = 0;       // LV_ALIGN_* value
    int alignOfsX = 0;
    int alignOfsY = 0;
    
    // Slider/progress
    int minValue = 0;
    int maxValue = 100;
    int curValue = 0;
    
    // Switch
    bool checked = false;
    
    // Input
    std::string placeholder;
    bool password = false;
    
    // Flex layout
    int flexFlow = -1;
    int flexGrow = 0;
    int flexMainAlign = 0;
    int flexCrossAlign = 0;
    int flexTrackAlign = 0;
    int padRow = 0;
    int padColumn = 0;
    bool scrollable = false;
    
    // Events/navigation
    std::string onclick;
    std::string onhold;
    std::string onenter;
    std::string onblur;
    std::string href;
    
    std::vector<Widget*> children;
    Widget* parent = nullptr;
    
    // === Query API ===
    
    int count(const char* type, bool recursive = false) const {
        int c = 0;
        for (auto* child : children) {
            if (child->type == type) c++;
            if (recursive) c += child->count(type, true);
        }
        return c;
    }
    
    Widget* find(const char* type, bool recursive = false) const {
        for (auto* child : children) {
            if (child->type == type) return child;
            if (recursive) {
                Widget* found = child->find(type, true);
                if (found) return found;
            }
        }
        return nullptr;
    }
    
    Widget* findByText(const char* txt, bool recursive = true) const {
        for (auto* child : children) {
            if (child->text == txt) return child;
            if (recursive) {
                Widget* found = child->findByText(txt, true);
                if (found) return found;
            }
        }
        return nullptr;
    }
    
    Widget* findById(const char* targetId, bool recursive = true) const {
        for (auto* child : children) {
            if (child->id == targetId) return child;
            if (recursive) {
                Widget* found = child->findById(targetId, true);
                if (found) return found;
            }
        }
        return nullptr;
    }
    
    std::vector<Widget*> findAll(const char* type, bool recursive = false) const {
        std::vector<Widget*> result;
        for (auto* child : children) {
            if (child->type == type) result.push_back(child);
            if (recursive) {
                auto sub = child->findAll(type, true);
                result.insert(result.end(), sub.begin(), sub.end());
            }
        }
        return result;
    }
    
    Widget* child(int index) const {
        if (index >= 0 && index < (int)children.size())
            return children[index];
        return nullptr;
    }
    
    Widget* first(const char* type) const {
        return find(type, false);
    }
};

// === Global state ===
inline std::vector<Widget*> g_all;
inline Widget* g_screen = nullptr;
inline int g_counter = 0;

inline void reset() {
    for (auto* w : g_all) delete w;
    g_all.clear();
    g_screen = nullptr;
    g_counter = 0;
}

inline void create_screen(int w, int h) {
    reset();
    g_screen = new Widget();
    g_screen->type = "Screen";
    g_screen->id = "screen";
    g_screen->w = w;
    g_screen->h = h;
    g_all.push_back(g_screen);
}

inline Widget* create(const char* type, Widget* parent) {
    Widget* w = new Widget();
    w->type = type;
    w->id = std::string(type) + "_" + std::to_string(g_counter++);
    w->parent = parent ? parent : g_screen;
    if (w->parent) {
        w->parent->children.push_back(w);
    }
    g_all.push_back(w);
    return w;
}

// === KDL Output ===
inline std::string to_kdl(Widget* w, int indent = 0) {
    std::string pad(indent * 4, ' ');
    std::ostringstream ss;
    
    ss << pad << w->type << " {\n";
    ss << pad << "    id \"" << w->id << "\"\n";
    
    if (w->x != 0 || w->y != 0)
        ss << pad << "    x " << w->x << "; y " << w->y << "\n";
    
    if (w->w != 0 || w->h != 0)
        ss << pad << "    width " << w->w << "; height " << w->h << "\n";
    
    if (!w->text.empty())
        ss << pad << "    text \"" << w->text << "\"\n";
    
    if (w->hasColor)
        ss << std::hex << pad << "    color 0x" << w->color << std::dec << "\n";
    
    if (w->hasBgcolor)
        ss << std::hex << pad << "    bgcolor 0x" << w->bgcolor << std::dec << "\n";
    
    if (w->fontSize > 0)
        ss << pad << "    font " << w->fontSize << "\n";
    
    if (w->radius > 0)
        ss << pad << "    radius " << w->radius << "\n";
    
    if (w->hidden)
        ss << pad << "    visible false\n";
    
    // Text alignment
    if (w->textAlignH != 0) {
        const char* h[] = {"left", "center", "right"};
        ss << pad << "    text-align-h \"" << h[w->textAlignH] << "\"\n";
    }
    if (w->padTop > 0) {
        ss << pad << "    pad-top " << w->padTop << "\n";
    }
    
    // Element alignment
    if (w->alignType != 0) {
        ss << pad << "    align " << w->alignType;
        if (w->alignOfsX != 0 || w->alignOfsY != 0)
            ss << " ofs " << w->alignOfsX << " " << w->alignOfsY;
        ss << "\n";
    }
    
    // Slider/progress
    if (w->type == "Slider" || w->type == "Bar") {
        ss << pad << "    range " << w->minValue << " " << w->maxValue << "\n";
        ss << pad << "    value " << w->curValue << "\n";
    }
    
    // Switch
    if (w->type == "Switch" && w->checked) {
        ss << pad << "    checked true\n";
    }
    
    for (auto* child : w->children) {
        ss << to_kdl(child, indent + 1);
    }
    
    ss << pad << "}\n";
    return ss.str();
}

inline std::string to_kdl() {
    return g_screen ? to_kdl(g_screen) : "";
}

// === Legacy API ===
inline int count(const char* type) {
    return g_screen ? g_screen->count(type, true) : 0;
}

inline Widget* find_by_text(const char* text) {
    return g_screen ? g_screen->findByText(text, true) : nullptr;
}

} // namespace LvglMock
