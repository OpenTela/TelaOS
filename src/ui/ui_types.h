/**
 * ui_types.h - UI engine data types
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include "lvgl.h"
#include "utils/psram_alloc.h"
#include "widgets/widget_common.h"

namespace UI {

// Tag constants
namespace Tag {
    constexpr const char* App = "app";
    
    namespace Layout {
        constexpr const char* UI = "ui";
        constexpr const char* Page = "page";
        constexpr const char* Group = "group";
    }
    
    namespace Meta {
        constexpr const char* Config = "config";
        constexpr const char* State = "state";
        constexpr const char* Timer = "timer";
        constexpr const char* Script = "script";
        constexpr const char* Style = "style";
    }
    
    namespace Element {
        constexpr const char* Label = "label";
        constexpr const char* Button = "button";
        constexpr const char* Switch = "switch";
        constexpr const char* Slider = "slider";
        constexpr const char* Input = "input";
        constexpr const char* Canvas = "canvas";
        constexpr const char* Image = "image";
        constexpr const char* Markdown = "markdown";
        constexpr const char* Tabs = "tabs";
        constexpr const char* Tab = "tab";
    }
    
    namespace TypeTag {
        constexpr const char* String = "string";
        constexpr const char* Int = "int";
        constexpr const char* Bool = "bool";
        constexpr const char* Float = "float";
    }
}

enum class Orientation {
    Horizontal,
    Vertical
};

namespace Modern {

struct Variable {
    P::String name;
    P::String type;
    P::String default_val;
    
    Variable() = default;
    Variable(const P::String& n, const P::String& t, const P::String& def = "");
};

struct Timer {
    int interval_ms = 0;
    P::String callback;
    
    Timer() = default;
    Timer(int ms, const P::String& cb);
};

struct StyleProperty {
    P::String name;
    P::String value;
    
    StyleProperty() = default;
    StyleProperty(const P::String& n, const P::String& v);
    
    int toInt() const;
    uint32_t toColor() const;
};

struct Style {
    P::String id;
    P::Array<StyleProperty> properties;
    
    void addProperty(const P::String& name, const P::String& value);
};

struct Element {
    P::String id;
    Widget w;                     // LVGL object wrapper (replaces raw obj pointer)
    lv_obj_t* parentObj = nullptr;
    
    // Compatibility getter
    lv_obj_t* obj() const { return w.handle; }
    
    // Box for container-level operations (bgcolor, visible, position)
    Widget box() const { return Widget{ parentObj ? parentObj : w.handle }; }
    
    P::String href;
    P::String onclick;
    P::String onchange;
    P::String ontap;
    P::String onhold;
    
    P::String bind;
    P::String tpl;
    P::String classTemplate;
    P::String visibleBind;
    P::String bgcolorBind;
    P::String colorBind;
    
    uint8_t* canvasBuffer = nullptr;
    int canvasWidth = 0;
    int canvasHeight = 0;
    
    bool is_page = false;
    bool is_canvas = false;
    int zIndex = 0;
    uint32_t last_update = 0;
    
    std::function<void(lv_event_t*)> onClickFn;
    std::function<void(lv_event_t*)> onValueChangedFn;
    std::function<void(lv_event_t*)> onFocusFn;
    std::function<void(lv_event_t*)> onDefocusFn;
    
    // Custom update callback for non-label widgets (e.g. markdown spangroup)
    // Called with rendered text instead of lv_label_set_text
    std::function<void(const P::String&)> updateFn;
    
    Element() = default;
    ~Element() = default;
};

struct Script {
    P::String code;
    P::String language = "lua";
};

} // namespace Modern

using OnClickHandler = std::function<void(const P::String& func_name)>;
using OnStateChangeHandler = std::function<void(const P::String& var_name, const P::String& value)>;

using Timer = Modern::Timer;
using Style = Modern::Style;
using Element = Modern::Element;
using Variable = Modern::Variable;
using StyleProperty = Modern::StyleProperty;

} // namespace UI
