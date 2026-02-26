/**
 * ui_html.cpp - HTML UI engine for LVGL
 * C++17 implementation with UI::Engine class
 */

#include "lvgl.h"
#include "ui/ui_html.h"
#include "ui/ui_engine.h"
#include "core/sys_paths.h"
#include "ui/ui_html_internal.h"
#include "ui/ui_types.h"
#include "ui/html_parser.h"
#include "ui/css_parser.h"
#include "utils/string_utils.h"
#include "ui/xml_utils.h"
#include "utils/font.h"
#include "widgets/widget_common.h"
#include "widgets/widget_methods.h"
#include "widgets/widget_label.h"
#include "widgets/widget_switch.h"
#include <unordered_map>
#include "widgets/widget_slider.h"
#include "widgets/widget_input.h"
#include "widgets/widget_image.h"
#include "widgets/widget_button.h"
#include "core/state_store.h"
#include "hal/display_hal.h"
#include "ble/ble_bridge.h"
#include "utils/log_config.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <Arduino.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>

static const char *TAG = "ui_html";

// ============ CONSTANTS ============
namespace {

constexpr size_t MAX_PAGES_PER_GROUP = 4;
constexpr size_t MAX_PAGE_CONTENTS = 64;

constexpr size_t TAG_BUF_LEN = 32;
constexpr size_t PATTERN_BUF_LEN = 48;
constexpr size_t VAR_NAME_LEN = 32;

} // anonymous namespace

// Use utility namespaces
using namespace UI::StringUtils;
using namespace UI::XmlUtils;
using namespace UI::Tag;

// State vars stored in State::store()

P::Array<UI::Timer> timers;
static P::Array<UI::Style> styles;
MPArray<UI::Element> elements;
P::Array<UI::PageGroup> groups;

P::String script_code;
P::String script_lang = "lua";
P::String app_version = "0.0";
P::String app_os_requirement;
P::String app_icon;
P::String app_path;  // Path to current app for resources
P::String ui_default_page;  // <ui default="/page"> — initial page to show
P::Array<P::String> s_iconPaths;   // Persistent storage for icon paths (LVGL needs valid pointers)
P::Array<P::String> s_imagePaths;  // Persistent storage for image paths

// CSS z-index deferred: CSS runs before store_element, so cache here
static std::unordered_map<lv_obj_t*, int> s_deferredZIndex;

// Resolve resource path - if relative, prepend app_path/resources/
// resolve_resource_path moved to ui_widget_builder.cpp
bool app_readonly = false;

// rgb_to_native() is in ui_html_internal.h

// Global handlers
void (*g_onclick_handler)(const char* func_name) = nullptr;
void (*g_ontap_handler)(const char* func_name, int x, int y) = nullptr;
void (*g_onhold_handler)(const char* func_name) = nullptr;
void (*g_onhold_xy_handler)(const char* func_name, int x, int y) = nullptr;
void (*g_state_change_handler)(const char* var_name, const char* value) = nullptr;

// Flag to prevent recursion when updating widgets from state
bool g_updating_from_binding = false;

// Standalone pages (not in group)
P::Array<P::String> page_ids;
P::Array<lv_obj_t*> page_objs;
lv_obj_t *g_keyboards[MAX_SCREENS] = {nullptr};
int page_count = 0;
static int current_page = 0;

// Current group (-1 = standalone page)
static int current_group = INVALID_INDEX;

static lv_obj_t *get_screen(void) {
    return lv_screen_active();
}

// ============ HEAD SECTION PARSING ============

// Parse variables from ParsedElement into Store
static void parse_vars_to_store(const UI::ParsedElement& section, Store& store) {
    for (const auto& var : section.children) {
        auto name = var.get("name");
        if (name.empty()) continue;
        
        P::String nameStr(name);
        
        if (var.is("bool")) {
            store.defineBool(nameStr, var.getBool("default"));
        } else if (var.is("int")) {
            store.defineInt(nameStr, var.getInt("default"));
        } else if (var.is("float")) {
            store.defineFloat(nameStr, var.getFloat("default"));
        } else if (var.is("string")) {
            store.defineString(nameStr, P::String(var.get("default")));
        }
    }
}

// Parse <state> section
static void parse_state(const UI::ParsedElement& root) {
    auto state = root.find("state");
    if (!state) return;
    
    LOG_D(Log::UI, "state:");
    parse_vars_to_store(*state, State::store());
}

// Parse <timer> tags
static void parse_timers(const UI::ParsedElement& root) {
    auto timerElems = root.findAll("timer");
    
    for (const auto* t : timerElems) {
        UI::Timer timer;
        timer.interval_ms = t->getInt("interval");
        auto callback = t->get("call");
        if (!callback.empty()) {
            timer.callback = P::String(callback);
        }
        
        if (timer.interval_ms > 0 && !timer.callback.empty()) {
            LOG_D(Log::UI, "timer: %dms -> %s()", timer.interval_ms, timer.callback.c_str());
            timers.push_back(std::move(timer));
        }
    }
}

// Parse <script language='...'> section
static void parse_script(const UI::ParsedElement& root) {
    auto script = root.find("script");
    if (!script) {
        LOG_W(Log::UI, "No <script> tag found!");
        return;
    }
    
    auto lang = script->get("language", "lua");
    script_lang = P::String(lang);
    // Normalize to lowercase
    for (char& c : script_lang) c = tolower((unsigned char)c);
    
    // Get script content (text inside <script>...</script>)
    script_code = script->text;
    
    LOG_D(Log::UI, "script raw text: %d bytes", (int)script_code.size());
    
    // Trim leading whitespace
    size_t start = script_code.find_first_not_of(" \t\n\r");
    if (start != P::String::npos && start > 0) {
        script_code.erase(0, start);
    }
    
    LOG_D(Log::UI, "script: %s (%d bytes)", script_lang.c_str(), (int)script_code.size());
    
    // Debug: show first 100 chars
    if (script_code.size() > 0) {
        auto preview = script_code.substr(0, 100);
        LOG_D(Log::UI, "script preview: %.100s...", preview.c_str());
    }
}

// Parse <style> section
static void parse_style(const UI::ParsedElement& root) {
    auto style = root.find("style");
    if (!style) return;
    
    // Get CSS content
    if (!style->text.empty()) {
        UI::Css::instance().parse(style->text);
        LOG_D(Log::UI, "style: parsed CSS");
    }
}

// Parse <config> section
// Subtags: <network/>, <display buffer="..."/>
static void parse_system(const UI::ParsedElement& root) {
    auto system = root.find("config");
    if (!system) return;
    
    // Check for <display buffer="..."/>
    auto display = system->find("display");
    DisplayBuffer targetBuffer = BufferOptimal;  // default: 1/8 screen, above LVGL recommendation
    bool hasDisplayTag = false;
    
    if (display) {
        hasDisplayTag = true;
        auto buffer = display->get("buffer");
        if (buffer == "micro" || buffer == "min") {
            targetBuffer = BufferMicro;
        } else if (buffer == "small") {
            targetBuffer = BufferSmall;
        } else if (buffer == "optimal" || buffer == "medium") {
            targetBuffer = BufferOptimal;
        } else if (buffer == "max") {
            targetBuffer = BufferMax;
        } else if (buffer == "auto" || buffer.empty()) {
            targetBuffer = BufferOptimal;
        }
        LOG_I(Log::UI, "system: display buffer=%.*s (%d lines)", (int)buffer.size(), buffer.data(), targetBuffer);
    }
    
    // Check for <network/> — enables BLE bridge for HTTP requests
    auto network = system->find("network");
    if (network) {
        auto mode = network->get("mode");
        
        // Already running? Skip all memory gymnastics
        if (BLEBridge::isInitialized()) {
            LOG_I(Log::UI, "config: network — BLE already running");
        } else {
            // BLE needs ~40KB DRAM. Check if we need to reduce buffer
            size_t freeDram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            const size_t BLE_REQUIRED = 40000;
            
            if (freeDram < BLE_REQUIRED) {
                Serial.printf("[system] BLE needs %u bytes, only %u free - reducing buffer\n", 
                             BLE_REQUIRED, freeDram);
                display_set_buffer(BufferSmall);
                freeDram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                Serial.printf("[system] After buffer reduction: %u bytes free\n", freeDram);
            } else if (!hasDisplayTag) {
                targetBuffer = BufferOptimal;
                if (display_get_buffer_lines() > targetBuffer) {
                    display_set_buffer(targetBuffer);
                }
            }
            
            freeDram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            if (freeDram < BLE_REQUIRED) {
                LOG_E(Log::UI, "system: NOT ENOUGH MEMORY FOR BLE! Need %u, have %u", BLE_REQUIRED, freeDram);
                State::store().set("_error", P::String("BLE: not enough memory"));
                return;
            }
            
            LOG_I(Log::UI, "system: initializing BLE...");
            Serial.printf("[Heap] Before BLE: %d bytes free\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            BLEBridge::init("FutureClock");
            Serial.printf("[Heap] After BLE: %d bytes free\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }
        
        if (mode == "ondemand") {
            LOG_I(Log::UI, "config: network mode=ondemand");
        } else {
            LOG_I(Log::UI, "config: network enabled");
        }
        Serial.flush();
    } else if (hasDisplayTag) {
        // Only display tag, no network - apply buffer setting
        display_set_buffer(targetBuffer);
    }
}

// Parse metadata sections (config, state, timer, script, style) from <app>
static void parse_head(const char *html) {
    // Clear previous CSS
    UI::Css::instance().clear();
    
    // Parse using new C++ parser
    auto doc = UI::Parser::parse(html);
    
    LOG_D(Log::UI, "parse_head: doc has %d children", (int)doc.children.size());
    for (const auto& child : doc.children) {
        LOG_D(Log::UI, "  child: <%s> text=%d bytes", child.tag.c_str(), (int)child.text.size());
    }
    
    // Find <app> element
    auto* app = doc.find("app");
    if (!app) {
        LOG_W(Log::UI, "No <app> found, using root");
        // Fallback: use root
        parse_state(doc);
        parse_timers(doc);
        parse_script(doc);
        parse_style(doc);
        parse_system(doc);
        return;
    }
    
    LOG_D(Log::UI, "<app> found with %d children", (int)app->children.size());
    for (const auto& child : app->children) {
        LOG_D(Log::UI, "  app child: <%s> text=%d bytes", child.tag.c_str(), (int)child.text.size());
    }
    
    // Read app version
    auto ver = app->get("version");
    if (!ver.empty()) {
        app_version = P::String(ver);
        LOG_D(Log::UI, "App version: %s", app_version.c_str());
    }
    
    // Read OS requirement
    auto os = app->get("os");
    if (!os.empty()) {
        app_os_requirement = P::String(os);
        LOG_D(Log::UI, "App requires OS: %s", app_os_requirement.c_str());
    }
    
    // Read readonly flag
    auto ro = app->get("readonly");
    app_readonly = (ro == "true" || ro == "1");
    if (app_readonly) {
        LOG_D(Log::UI, "App is readonly");
    }
    
    // Read icon attribute: icon="system:puzzle-game" or icon="/path/to/icon.png"
    auto icon = app->get("icon");
    if (!icon.empty()) {
        // Check for system: prefix
        if (icon.substr(0, 7) == "system:") {
            // System icon: system:puzzle-game -> /system/resources/icons/puzzle-game.png
            auto iconName = icon.substr(7);
            P::String fullPath = P::String(SYS_ICONS) + P::String(iconName) + ".png";
            app_icon = fullPath.c_str();
        } else {
            app_icon = P::String(icon).c_str();
        }
        LOG_D(Log::UI, "App icon: %s", app_icon.c_str());
    }
    
    parse_state(*app);
    parse_timers(*app);
    parse_script(*app);
    parse_style(*app);
    parse_system(*app);
    
    // Parse <ui default="/page">
    auto* uiNode = app->find("ui");
    if (uiNode) {
        auto def = uiNode->get("default");
        if (!def.empty()) {
            // Strip leading / if present
            if (def[0] == '/') def = def.substr(1);
            ui_default_page = P::String(def);
            LOG_I(Log::UI, "UI default page: %s", ui_default_page.c_str());
        }
    }
}

// ============ END HEAD PARSING ============

int find_page_index(const char *id) {
    for (int i = 0; i < page_count; i++) {
        if (page_ids[i] == id) return i;
    }
    return INVALID_INDEX;
}

// ============ COMMON ATTRIBUTES ============
// Shared attributes that all widgets can have

// CommonAttrs, parseCommonAttrs, ensureId, extractBindVar moved to ui_widget_builder.cpp

// Store element with all possible attributes
// ============ Element registration ============

int store_element(ElementDesc d) {
    if (!d.id || !d.id[0]) return INVALID_INDEX;
    
    auto el = P::create<UI::Element>();
    el->id = d.id;
    el->w.handle = d.obj;
    _unique_id(d.obj, d.id);  // For testing mock
    el->is_page = d.is_page;
    el->href     = (d.href     && d.href[0])     ? d.href     : "";
    el->onclick  = (d.onclick  && d.onclick[0])  ? d.onclick  : "";
    el->onchange = (d.onchange && d.onchange[0]) ? d.onchange : "";
    el->bind     = (d.bind     && d.bind[0])     ? d.bind     : "";
    el->tpl      = (d.tpl && strchr(d.tpl, '{')) ? d.tpl : "";
    el->classTemplate = (d.classTpl && strchr(d.classTpl, '{')) ? d.classTpl : "";
    
    // Parse visibleBind - extract var name from "{varname}"
    if (d.visibleBind && d.visibleBind[0]) {
        el->visibleBind = extractBindVar(d.visibleBind);
        
        // Apply initial visibility
        if (!el->visibleBind.empty()) {
            P::String val = State::store().getString(el->visibleBind);
            bool visible = (val == "true" || val == "1");
            if (!visible) {
                lv_obj_add_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    // Parse bgcolorBind - extract var name from "{varname}"
    if (d.bgcolorBind && d.bgcolorBind[0]) {
        el->bgcolorBind = extractBindVar(d.bgcolorBind);
        LOG_D(Log::UI, "store_element: id=%s bgcolorBind=%s (from %s)", d.id, el->bgcolorBind.c_str(), d.bgcolorBind);
    }
    
    // Parse colorBind - extract var name from "{varname}"
    if (d.colorBind && d.colorBind[0]) {
        el->colorBind = extractBindVar(d.colorBind);
    }
    
    int idx = (int)elements.size();
    elements.push_back(std::move(el));
    
    // Store z-index from HTML attribute
    if (d.zIndex != 0 && idx >= 0) {
        elements[idx]->zIndex = d.zIndex;
    }
    
    // Apply deferred CSS z-index (CSS runs before store_element)
    if (!s_deferredZIndex.empty() && d.obj) {
        auto it = s_deferredZIndex.find(d.obj);
        if (it != s_deferredZIndex.end()) {
            elements[idx]->zIndex = it->second;
            s_deferredZIndex.erase(it);
        }
    }
    
    return idx;
}

// Page-only shorthand
int store_page(const char* id, lv_obj_t* obj) {
    ElementDesc d;
    d.id = id;
    d.obj = obj;
    d.is_page = true;
    return store_element(d);
}

void setElementZIndex(lv_obj_t* handle, int z) {
    for (auto& el : elements) {
        if (el->w.handle == handle || el->parentObj == handle) {
            el->zIndex = z;
            return;
        }
    }
    // Element not stored yet (CSS runs before store_element) — defer
    s_deferredZIndex[handle] = z;
}

// Post-render: reorder children by z-index
// Negative z-index → move to back (most negative first, so it ends up deepest)
// Positive z-index → move to front (least positive first, most positive ends on top)
static void applyZIndexOrdering() {
    // Collect elements with z-index set
    struct ZEntry { lv_obj_t* obj; int z; };
    P::Array<ZEntry> negatives, positives;
    
    for (const auto& el : elements) {
        if (el->zIndex == 0 || el->is_page) continue;
        lv_obj_t* target = el->parentObj ? el->parentObj : el->w.handle;
        if (!target) continue;
        
        if (el->zIndex < 0) negatives.push_back({target, el->zIndex});
        else                 positives.push_back({target, el->zIndex});
    }
    
    // Negatives: descending order (least negative first → index 0, then most negative → index 0)
    std::sort(negatives.begin(), negatives.end(), [](const ZEntry& a, const ZEntry& b) {
        return a.z > b.z;  // -1 before -2
    });
    for (auto& e : negatives) {
        lv_obj_move_to_index(e.obj, 0);
    }
    
    // Positives: ascending order (least positive first → foreground, most positive last → top)
    std::sort(positives.begin(), positives.end(), [](const ZEntry& a, const ZEntry& b) {
        return a.z < b.z;  // 1 before 2
    });
    for (auto& e : positives) {
        lv_obj_move_foreground(e.obj);
    }
}

lv_obj_t *ui_get_internal(const char *id) {
    for (const auto& el : elements) {
        if (el->id == id) return el->obj();
    }
    return nullptr;
}

bool ui_trigger_click(const char *id) {
    for (const auto& el : elements) {
        if (el->id == id && !el->onclick.empty() && g_onclick_handler) {
            g_onclick_handler(el->onclick.c_str());
            return true;
        }
    }
    return false;
}

bool ui_call_function(const char *funcName) {
    if (!funcName || !funcName[0]) return false;
    if (g_onclick_handler) {
        g_onclick_handler(funcName);
    }
    return true;
}

void ui_set_text_internal(const char *id, const char *text) {
    lv_obj_t *obj = ui_get_internal(id);
    if (!obj) return;
    
    // TODO: Наши шрифты поддерживают только ASCII + кириллицу.
    // Нужно сделать warning или горячую замену Unicode символов.
    if (lv_obj_get_child_cnt(obj) > 0) {
        lv_label_set_text(lv_obj_get_child(obj, 0), text);
    } else {
        lv_label_set_text(obj, text);
    }
}

void ui_set_switch(const char *id, bool checked) {
    lv_obj_t *obj = ui_get_internal(id);
    if (!obj) return;
    if (checked) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
}

void ui_set_slider(const char *id, int value) {
    lv_obj_t *obj = ui_get_internal(id);
    if (!obj) return;
    lv_slider_set_value(obj, value, LV_ANIM_ON);
}

void ui_set_input(const char *id, const char *text) {
    lv_obj_t *obj = ui_get_internal(id);
    if (!obj) return;
    lv_textarea_set_text(obj, text ? text : "");
}

// Get state variable value by name - uses StateStore
const char *get_state_value(const char *name) {
    return state_store_get(name);
}

// Set state variable value - uses StateStore (silent - no UI callback)
void ui_set_state(const char *name, const char *value) {
    state_store_set_silent(name, value);
}

// Render template: replace {var} with state values
P::String render_template(const char *tpl) {
    P::String result;
    const char *p = tpl;
    
    while (*p) {
        if (*p == '{') {
            // Find closing }
            const char *close = strchr(p, '}');
            if (close && (close - p) < VAR_NAME_LEN) {
                // Extract variable name
                P::String varname(p + 1, close - p - 1);
                
                // Get value and append
                const char *val = get_state_value(varname.c_str());
                result += val;
                p = close + 1;
            } else {
                result += *p++;
            }
        } else {
            result += *p++;
        }
    }
    return decodeEntities(result);
}

// Check if template contains {varname}
static bool template_has_var(const char *tpl, const char *varname) {
    if (!tpl || !tpl[0]) return false;
    
    char search[PATTERN_BUF_LEN];
    snprintf(search, sizeof(search), "{%s}", varname);
    return strstr(tpl, search) != nullptr;
}

// Update all elements that bind to this variable
// Forward declarations
static void ui_update_bindings_internal(const char *varname, const char *value);
uint32_t parse_color(const char *s);
int32_t parse_coord_w(const char *s);
int32_t parse_coord_h(const char *s);

// Flag to track if we're inside LVGL callback
bool s_in_lvgl_callback = false;

// Public function - calls internal directly (we're always in LVGL context)
void ui_update_bindings(const char *varname, const char *value) {
    ui_update_bindings_internal(varname, value);
}

// Internal function that actually updates bindings
static void ui_update_bindings_internal(const char *varname, const char *value) {
    LOG_V(Log::UI, "update_bindings: var=%s val=%s elements=%d", varname, value, (int)elements.size());
    
    // First update internal state
    ui_set_state(varname, value);
    
    // Prevent recursion
    g_updating_from_binding = true;
    
    // Then find and update all elements
    for (size_t i = 0; i < elements.size(); i++) {
        if (!elements[i]) {
            continue;
        }
        
        lv_obj_t *obj = elements[i]->obj();
        if (!obj) {
            continue;
        }
        
        // Update templates with {varname}
        if (template_has_var(elements[i]->tpl.c_str(), varname)) {
            LOG_V(Log::UI, "update_bindings: updating element %d tpl=%s", (int)i, elements[i]->tpl.c_str());
            P::String rendered = render_template(elements[i]->tpl.c_str());
            LOG_V(Log::UI, "update_bindings: rendered=%s", rendered.c_str());
            
            // Custom update handler (e.g. markdown spangroup)
            if (elements[i]->updateFn) {
                elements[i]->updateFn(rendered);
            }
            else if (lv_obj_get_child_cnt(obj) > 0) {
                lv_obj_t *child = lv_obj_get_child(obj, 0);
                if (child) {
                    const char* cur = lv_label_get_text(child);
                    if (!cur || strcmp(cur, rendered.c_str()) != 0) {
                        lv_label_set_text(child, rendered.c_str());
                    }
                }
            } else {
                const char* cur = lv_label_get_text(obj);
                if (!cur || strcmp(cur, rendered.c_str()) != 0) {
                    lv_label_set_text(obj, rendered.c_str());
                }
            }
        }
        
        // Update dynamic class with {varname}
        if (template_has_var(elements[i]->classTemplate.c_str(), varname)) {
            P::String renderedClass = render_template(elements[i]->classTemplate.c_str());
            
            // For buttons: apply styles to parentObj (the button), not obj (the label inside)
            lv_obj_t *styleObj = elements[i]->parentObj ? elements[i]->parentObj : obj;
            if (!styleObj) {
                continue;
            }
            
            // Save position and size before removing styles
            lv_coord_t x = lv_obj_get_x(styleObj);
            lv_coord_t y = lv_obj_get_y(styleObj);
            lv_coord_t w = lv_obj_get_width(styleObj);
            lv_coord_t h = lv_obj_get_height(styleObj);
            
            // Reset styles and reapply class
            lv_obj_remove_style_all(styleObj);
            Widget{styleObj}.applyStyle(renderedClass);
            
            // Restore position and size
            lv_obj_set_pos(styleObj, x, y);
            lv_obj_set_size(styleObj, w, h);
            
            // For buttons: also update text color on the label inside
            if (elements[i]->parentObj && elements[i]->obj()) {
                lv_obj_t *lbl = elements[i]->obj();
                // Find text color in rendered class
                std::istringstream iss(renderedClass);
                P::String cls;
                while (iss >> cls) {
                    auto color = UI::Css::instance().prop(cls, "color");
                    if (!color.empty()) {
                        lv_obj_set_style_text_color(lbl, lv_color_hex(UI::Css::instance().getColor(cls, "color")), LV_PART_MAIN);
                    }
                }
            }
            
            LOG_V(Log::UI, "Dynamic class update: %s -> %s", elements[i]->classTemplate.c_str(), renderedClass.c_str());
        }
        
        // Update widgets with bind=varname
        if (!elements[i]->bind.empty() && elements[i]->bind == varname) {
            // Check widget type and update
            if (lv_obj_check_type(obj, &lv_switch_class)) {
                bool checked = (toBool(value));
                bool current = lv_obj_has_state(obj, LV_STATE_CHECKED);
                if (checked != current) {
                    if (checked) {
                        lv_obj_add_state(obj, LV_STATE_CHECKED);
                    } else {
                        lv_obj_clear_state(obj, LV_STATE_CHECKED);
                    }
                }
            } else if (lv_obj_check_type(obj, &lv_slider_class)) {
                int newVal = atoi(value);
                if (lv_slider_get_value(obj) != newVal) {
                    lv_slider_set_value(obj, newVal, LV_ANIM_OFF);
                }
            } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
                // Skip if text is already the same - prevents cursor reset during typing
                const char* current = lv_textarea_get_text(obj);
                if (!current || strcmp(current, value) != 0) {
                    lv_textarea_set_text(obj, value);
                }
            }
        }
        
        // Update visibility with visibleBind=varname
        if (!elements[i]->visibleBind.empty() && elements[i]->visibleBind == varname) {
            bool visible = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            UI::setVisible(elements[i]->box(), visible);
            LOG_V(Log::UI, "Visibility update: %s -> %s", elements[i]->id.c_str(), visible ? "visible" : "hidden");
        }
        
        // Update background color with bgcolorBind=varname
        if (!elements[i]->bgcolorBind.empty()) {
            LOG_V(Log::UI, "  element %s: bgcolorBind=%s (looking for %s)", 
                     elements[i]->id.c_str(), elements[i]->bgcolorBind.c_str(), varname);
        }
        if (!elements[i]->bgcolorBind.empty() && elements[i]->bgcolorBind == varname) {
            LOG_V(Log::UI, "Bgcolor update: %s -> %s", elements[i]->id.c_str(), value);
            UI::setBgColor(elements[i]->box(), parse_color(value));
        }
        
        // Update text color with colorBind=varname
        if (!elements[i]->colorBind.empty() && elements[i]->colorBind == varname) {
            UI::setColor(elements[i]->w, parse_color(value));
            LOG_V(Log::UI, "Color update: %s -> %s", elements[i]->id.c_str(), value);
        }
    }
    
    g_updating_from_binding = false;
}

// Sync all widgets with their bound state values (call after loadState)
// Find group by id
static int find_group_index(const char *id) {
    for (int i = 0; i < (int)groups.size(); i++) {
        if (groups[i].id == id) return i;
    }
    return INVALID_INDEX;
}

// Find page in group
static int find_page_in_group(UI::PageGroup *grp, const char *page_id) {
    for (int i = 0; i < (int)grp->page_ids.size(); i++) {
        if (grp->page_ids[i] == page_id) return i;
    }
    return INVALID_INDEX;
}

// Check if position is inside any <group>...</group> block
static bool isInsideGroup(const char* html, const char* pos) {
    const char *check = html;
    int iterations = 0;
    while (check < pos) {
        if (++iterations > 50) break;
        const char *gopen = findTagOpen(check, Layout::Group);
        if (!gopen || gopen > pos) break;
        
        const char *gclose = findTagClose(gopen + tagOpenLen(Layout::Group), Layout::Group);
        if (gclose && pos > gopen && pos < gclose) return true;
        check = gopen + 1;
    }
    return false;
}

// Create a fullscreen transparent page container
static lv_obj_t* createPageObj(lv_obj_t* parent) {
    lv_obj_t *scr = lv_obj_create(parent);
    lv_obj_set_size(scr, lv_pct(FULL_SIZE_PCT), lv_pct(FULL_SIZE_PCT));
    lv_obj_set_pos(scr, 0, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

void ui_show_page_internal(const char *path) {
    if (!path) return;
    
    // Skip leading /
    const char *p = path;
    if (*p == '/') p++;
    
    // Check if path contains / (group/page format)
    const char *slash = strchr(p, '/');
    
    if (slash) {
        // Path is group/page
        P::String group_id(p, slash - p);
        P::String page_id = slash + 1;
        
        int grp_idx = find_group_index(group_id.c_str());
        if (grp_idx < 0) {
            LOG_W(Log::UI, "Group not found: %s", group_id.c_str());
            return;
        }
        
        int page_idx = find_page_in_group(&groups[grp_idx], page_id.c_str());
        if (page_idx < 0) {
            LOG_W(Log::UI, "Page not found: %s/%s", group_id.c_str(), page_id.c_str());
            return;
        }
        
        // Hide all standalone pages
        for (int i = 0; i < page_count; i++) {
            lv_obj_add_flag(page_objs[i], LV_OBJ_FLAG_HIDDEN);
        }
        
        // Hide all groups except this one
        for (int i = 0; i < (int)groups.size(); i++) {
            if (i == grp_idx) {
                lv_obj_clear_flag(groups[i].tileview, LV_OBJ_FLAG_HIDDEN);
                if (groups[i].indicator_obj) {
                    lv_obj_clear_flag(groups[i].indicator_obj, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                lv_obj_add_flag(groups[i].tileview, LV_OBJ_FLAG_HIDDEN);
                if (groups[i].indicator_obj) {
                    lv_obj_add_flag(groups[i].indicator_obj, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        
        // Navigate to tile using tileview API
        lv_obj_set_tile_id(groups[grp_idx].tileview, page_idx, 0, LV_ANIM_OFF);
        groups[grp_idx].current_page_idx = page_idx;
        groups[grp_idx].updateIndicator(page_idx);
        
        current_group = grp_idx;
        current_page = INVALID_INDEX;
        
        LOG_I(Log::UI, "Navigate: %s/%s", group_id.c_str(), page_id.c_str());
    } else {
        // Path is just page_id - could be group or standalone page
        int grp_idx = find_group_index(p);
        if (grp_idx >= 0) {
            // It's a group - show default page
            const auto& def = !groups[grp_idx].default_page.empty() 
                ? groups[grp_idx].default_page : groups[grp_idx].page_ids[0];
            char _buf[64]; snprintf(_buf, sizeof(_buf), "%s/%s", groups[grp_idx].id.c_str(), def.c_str());
            ui_show_page_internal(_buf);
            return;
        }
        
        // It's a standalone page
        int idx = find_page_index(p);
        if (idx < 0) {
            LOG_W(Log::UI, "Page not found: %s", p);
            return;
        }
        
        // Hide all groups
        for (int i = 0; i < (int)groups.size(); i++) {
            lv_obj_add_flag(groups[i].tileview, LV_OBJ_FLAG_HIDDEN);
            if (groups[i].indicator_obj) {
                lv_obj_add_flag(groups[i].indicator_obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        // Hide keyboards
        for (int i = 0; i < page_count; i++) {
            if (g_keyboards[i]) {
                lv_obj_add_flag(g_keyboards[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        // Show/hide standalone pages
        for (int i = 0; i < page_count; i++) {
            if (i == idx) {
                lv_obj_clear_flag(page_objs[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(page_objs[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        current_group = INVALID_INDEX;
        current_page = idx;
        
        LOG_I(Log::UI, "Navigate: %s", p);
    }
}

void navigate(const char *href) {
    if (!href || href[0] != '/') return;
    LOG_I(Log::UI, "navigate: %s", href);
    const char *target = href + 1;
    
    if (strcmp(target, "next") == 0) {
        if (current_page < page_count - 1) {
            ui_show_page_internal(page_ids[current_page + 1].c_str());
        }
    } else if (strcmp(target, "prev") == 0 || strcmp(target, "back") == 0) {
        if (current_page > 0) {
            ui_show_page_internal(page_ids[current_page - 1].c_str());
        }
    } else {
        ui_show_page_internal(target);
    }
}

const char* ui_get_current_page_id() {
    if (current_group >= 0 && current_group < (int)groups.size()) {
        auto& g = groups[current_group];
        if (g.current_page_idx >= 0 && g.current_page_idx < (int)g.page_ids.size())
            return g.page_ids[g.current_page_idx].c_str();
    }
    if (current_page >= 0 && current_page < page_count)
        return page_ids[current_page].c_str();
    return "";
}

// Event handlers + widget builders moved to ui_widget_builder.cpp

// Forward declaration for recursive use by create_tabs
static void parse_children(const char *html, int len, lv_obj_t *parent);

// ============ Tabs widget ============
// <tabs id="t" x="0" y="0" w="100%" h="100%" barh="32">
//   <tab title="Edit">...widgets...</tab>
//   <tab title="Preview">...widgets...</tab>
// </tabs>

void create_tabs(const char* astart, const char* aend, const char* content, lv_obj_t* parent) {
    using namespace UI::XmlUtils;
    
    int32_t x = parse_coord_w(getAttr(astart, aend, "x").c_str());
    int32_t y = parse_coord_h(getAttr(astart, aend, "y").c_str());
    int32_t w = parse_coord_w(getAttr(astart, aend, "w").c_str());
    int32_t h = parse_coord_h(getAttr(astart, aend, "h").c_str());
    int barh = getAttrInt(astart, aend, "barh", 32);
    auto id = getAttr(astart, aend, "id");
    auto bgcolorAttr = getAttr(astart, aend, "bgcolor");
    auto colorAttr = getAttr(astart, aend, "color");
    
    // Create tabview
    lv_obj_t* tv = lv_tabview_create(parent);
    lv_tabview_set_tab_bar_position(tv, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tv, barh);
    
    // Position and size
    if (x || y) lv_obj_set_pos(tv, x, y);
    if (w > 0) lv_obj_set_width(tv, w);
    if (h > 0) lv_obj_set_height(tv, h);
    
    // Style tab bar
    lv_obj_t* bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(bar, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(bar, Font::get(UI::Font::SMALL), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    
    // Active tab button style
    lv_obj_set_style_text_color(bar, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0066FF), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    
    // Style content area
    lv_obj_t* cont = lv_tabview_get_content(tv);
    if (!bgcolorAttr.empty()) {
        uint32_t bgc = parse_color(bgcolorAttr.c_str());
        lv_obj_set_style_bg_color(cont, lv_color_hex(bgc), 0);
        lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    }
    
    if (!colorAttr.empty()) {
        uint32_t c = parse_color(colorAttr.c_str());
        lv_obj_set_style_text_color(cont, lv_color_hex(c), 0);
    }
    
    // Parse <tab> children
    const char* p = content;
    const char* end = content + strlen(content);
    
    while (p < end) {
        const char* tstart = strstr(p, "<tab");
        if (!tstart || tstart >= end) break;
        
        const char* ta = tstart + 4; // skip "<tab"
        // Make sure it's "<tab " or "<tab>" not "<table" etc
        if (*ta != ' ' && *ta != '>') {
            p = ta;
            continue;
        }
        
        // Get tab attributes
        const char* tab_astart = ta;
        while (ta < end && *ta != '>') ta++;
        if (ta >= end) break;
        const char* tab_aend = ta;
        ta++; // skip '>'
        
        P::String title = getAttr(tab_astart, tab_aend, "title");
        if (title.empty()) title = "Tab";
        
        // Find </tab>
        const char* tab_close = strstr(ta, "</tab>");
        if (!tab_close || tab_close > end) break;
        
        // Create tab
        lv_obj_t* tab_page = lv_tabview_add_tab(tv, title.c_str());
        
        // Style tab page
        lv_obj_set_style_bg_opa(tab_page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(tab_page, 0, 0);
        lv_obj_set_style_border_width(tab_page, 0, 0);
        
        // Parse widgets inside this tab
        int tab_content_len = tab_close - ta;
        parse_children(ta, tab_content_len, tab_page);
        
        LOG_I(Log::UI, "tab: '%s' (%d bytes content)", title.c_str(), tab_content_len);
        
        p = tab_close + 6; // skip "</tab>"
    }
    
    LOG_I(Log::UI, "tabs: id=%s barh=%d", id.empty() ? "?" : id.c_str(), barh);
}

// Parse children of a page (labels, buttons, etc.)
static void parse_children(const char *html, int len, lv_obj_t *parent) {
    const char *p = html;
    const char *end = html + len;
    char tag[TAG_BUF_LEN];
    
    while (p < end) {
        while (p < end && *p != '<') p++;
        if (p >= end) break;
        p++;
        
        if (*p == '/' || *p == '!') {
            while (p < end && *p != '>') p++;
            if (p < end) p++;
            continue;
        }
        
        // Read tag
        int ti = 0;
        while (p < end && !isspace((unsigned char)*p) && *p != '>' && *p != '/' && ti < TAG_BUF_LEN - 1) {
            tag[ti++] = tolower((unsigned char)*p++);
        }
        tag[ti] = '\0';
        if (!tag[0]) continue;
        
        LOG_I(Log::UI, "parse_tag: [%s]", tag);
        
        // Skip nested pages and tab tags (tabs are handled inside create_tabs)
        if (strcmp(tag, Layout::Page) == 0 || strcmp(tag, Element::Tab) == 0) {
            while (p < end && *p != '>') p++;
            if (p < end) p++;
            continue;
        }
        
        const char *astart = p;
        while (p < end && *p != '>') p++;
        if (p >= end) break;
        const char *aend = p;
        bool self_close = (p > html && *(p-1) == '/');
        p++;
        
        P::String content;
        if (!self_close) {
            const char *cstart = p;
            char closing[PATTERN_BUF_LEN];
            snprintf(closing, sizeof(closing), "</%s>", tag);
            const char *cend = strstr(p, closing);
            if (cend && cend < end) {
                content.assign(cstart, cend - cstart);
                p = cend + strlen(closing);
            }
        }
        
        if (strcmp(tag, Element::Label) == 0) {
            create_label(astart, aend, content.c_str(), parent);
        } else if (strcmp(tag, Element::Button) == 0) {
            create_button(astart, aend, content.c_str(), parent);
        } else if (strcmp(tag, Element::Switch) == 0) {
            create_switch(astart, aend, parent);
        } else if (strcmp(tag, Element::Slider) == 0) {
            create_slider(astart, aend, parent);
        } else if (strcmp(tag, Element::Input) == 0) {
            create_input(astart, aend, content.c_str(), parent);
        } else if (strcmp(tag, Element::Canvas) == 0) {
            create_canvas(astart, aend, parent);
        } else if (strcmp(tag, Element::Image) == 0) {
            create_image(astart, aend, parent);
        } else if (strcmp(tag, Element::Markdown) == 0) {
            create_markdown(astart, aend, content.c_str(), parent);
        } else if (strcmp(tag, Element::Tabs) == 0) {
            create_tabs(astart, aend, content.c_str(), parent);
        }
    }
}

void ui_html_init_internal(void) {
    // Clear all vectors
    elements.clear();
    s_deferredZIndex.clear();
    timers.clear();
    styles.clear();
    groups.clear();
    page_ids.clear();
    page_objs.clear();
    
    // Reset state
    page_count = 0;
    current_page = 0;
    current_group = INVALID_INDEX;
    g_updating_from_binding = false;
    
    // Reset script
    script_lang = "lua";
    script_code.clear();
    
    // Reset app metadata
    app_icon.clear();
    
    // Clear stores
    State::store().clear();
    
    // Clear keyboards array
    memset(g_keyboards, 0, sizeof(g_keyboards));
    
    // Disable scroll on main screen to prevent "jitter" on touch
    lv_obj_t* scr = lv_screen_active();
    if (scr) {
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    LOG_I(Log::UI, "Init v%s", UI::Engine::version());
}

int ui_html_render_internal(const char *html) {
    if (!html) return INVALID_INDEX;
    
    LOG_D(Log::UI, "render_internal: starting");
    
    const char *p = html;
    
    // Pass 0: Parse <head>/<app> sections
    LOG_D(Log::UI, "render_internal: parse_head...");
    parse_head(html);
    LOG_D(Log::UI, "render_internal: parse_head done");
    
    if (!app_version.empty() && app_version != "0.0") {
        LOG_D(Log::UI, "========================================");
        LOG_D(Log::UI, "  App v%s (engine v%s)", app_version.c_str(), UI::Engine::version());
        LOG_D(Log::UI, "========================================");
    }
    
    // Collect page content for later parsing (static to save stack)
    struct PageContent { const char *start; int len; lv_obj_t *parent; };
    static PageContent contents[MAX_PAGE_CONTENTS];
    int content_count = 0;
    
    LOG_D(Log::UI, "render_internal: Pass 1 - groups");
    // Pass 1: Find <group> tags and create tabviews
    p = html;
    while (*p) {
        const char *gstart = findTagOpen(p, Layout::Group);
        if (!gstart) break;
        
        p = gstart + tagOpenLen(Layout::Group);
        
        // Get attributes
        const char *astart = p;
        while (*p && *p != '>') p++;
        if (!*p) break;
        const char *aend = p;
        p++;
        
        // Create new group (use index, not pointer - pointer invalidates on push_back)
        groups.push_back(UI::PageGroup{});
        size_t gi = groups.size() - 1;
        
        groups[gi].id = getAttr(astart, aend, "id");
        if (groups[gi].id.empty()) {
            LOG_W(Log::UI, "group without id");
            groups.pop_back();
            continue;
        }
        
        groups[gi].default_page = getAttr(astart, aend, "default");
        
        groups[gi].orientation = UI::Orientation::Horizontal;
        {
            auto orient = getAttr(astart, aend, "orientation");
            if (orient == "vertical" || orient == "v") {
                groups[gi].orientation = UI::Orientation::Vertical;
            }
        }
        
        groups[gi].indicator = UI::IndicatorType::Scrollbar;
        {
            auto ind = getAttr(astart, aend, "indicator");
            if (ind == "dots") {
                groups[gi].indicator = UI::IndicatorType::Dots;
            } else if (ind == "none") {
                groups[gi].indicator = UI::IndicatorType::None;
            }
        }
        
        // Find </group>
        const char *group_content_start = p;
        const char *gclose = findTagClose(p, Layout::Group);
        if (!gclose) {
            LOG_E(Log::UI, "No </group> for %s", groups[gi].id.c_str());
            break;
        }
        
        groups[gi].create(get_screen());
        
        // Parse pages inside this group
        const char *pp = group_content_start;
        while (pp < gclose && (int)groups[gi].page_ids.size() < MAX_PAGES_PER_GROUP) {
            const char *pstart = findTagOpen(pp, Layout::Page);
            if (!pstart || pstart >= gclose) break;
            
            pp = pstart + tagOpenLen(Layout::Page);
            
            const char *pastart = pp;
            while (pp < gclose && *pp != '>') pp++;
            if (pp >= gclose) break;
            const char *paend = pp;
            pp++;
            
            P::String page_id = getAttr(pastart, paend, "id");
            if (page_id.empty()) {
                LOG_W(Log::UI, "page without id in group %s", groups[gi].id.c_str());
                continue;
            }
            
            const char *page_content_start = pp;
            const char *pclose = findTagClose(pp, Layout::Page);
            if (!pclose || pclose > gclose) {
                LOG_E(Log::UI, "No </page> for %s in group %s", page_id.c_str(), groups[gi].id.c_str());
                break;
            }
            
            lv_obj_t *tile = groups[gi].addTile(page_id);
            
            // Apply page bgcolor if specified
            auto tileBgcolor = getAttr(pastart, paend, "bgcolor");
            if (!tileBgcolor.empty()) {
                uint32_t bgc = parse_color(tileBgcolor.c_str());
                lv_obj_set_style_bg_color(tile, lv_color_hex(bgc), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, LV_PART_MAIN);
            }
            
            // Store for later content parsing
            contents[content_count].start = page_content_start;
            contents[content_count].len = pclose - page_content_start;
            contents[content_count].parent = tile;
            content_count++;
            
            char _buf[64]; snprintf(_buf, sizeof(_buf), "%s/%s", groups[gi].id.c_str(), page_id.c_str());
            P::String full_id = _buf;
            store_page(full_id.c_str(), tile);
            
            LOG_D(Log::UI, "  page: %s (tile %d)", full_id.c_str(), (int)groups[gi].page_ids.size() - 1);
            
            pp = pclose + tagCloseLen(Layout::Page);
        }
        
        groups[gi].finalize((int)gi);
        
        // Hide if not first
        if (groups.size() > 1 || page_count > 0) {
            groups[gi].hide();
        }
        
        store_page(groups[gi].id.c_str(), groups[gi].tileview);
        LOG_D(Log::UI, "group: %s (%d pages, %s)", groups[gi].id.c_str(), (int)groups[gi].page_ids.size(),
                 groups[gi].isHorizontal() ? "horizontal" : "vertical");
        
        p = gclose + tagCloseLen(Layout::Group);
    }
    
    LOG_D(Log::UI, "render_internal: Pass 2 - standalone pages");
    // Pass 2: Find standalone <page> tags (not inside groups)
    p = html;
    int pass2_iterations = 0;
    while (*p && page_count < MAX_SCREENS) {
        if (++pass2_iterations > 100) {
            LOG_E(Log::UI, "Pass 2: too many iterations, breaking");
            break;
        }
        
        const char *pstart = findTagOpen(p, Layout::Page);
        if (!pstart) break;
        
        // Skip pages inside groups
        if (isInsideGroup(html, pstart)) {
            p = pstart + tagOpenLen(Layout::Page);
            while (*p && *p != '>') p++;
            if (*p) p++;
            const char *pclose = findTagClose(p, Layout::Page);
            if (pclose) p = pclose + tagCloseLen(Layout::Page);
            continue;
        }
        
        p = pstart + tagOpenLen(Layout::Page);
        
        const char *astart = p;
        while (*p && *p != '>') p++;
        if (!*p) break;
        const char *aend = p;
        p++;
        
        P::String id = getAttr(astart, aend, "id");
        if (id.empty()) {
            LOG_W(Log::UI, "page without id, skipping");
            const char *pclose = findTagClose(p, Layout::Page);
            if (pclose) p = pclose + tagCloseLen(Layout::Page);
            continue;
        }
        
        const char *content_start = p;
        const char *pclose = findTagClose(p, Layout::Page);
        if (!pclose) {
            LOG_E(Log::UI, "No </page> for %s", id.c_str());
            break;
        }
        
        lv_obj_t *scr = createPageObj(get_screen());
        
        // Apply page bgcolor if specified
        auto pageBgcolor = getAttr(astart, aend, "bgcolor");
        if (!pageBgcolor.empty()) {
            uint32_t bgc = parse_color(pageBgcolor.c_str());
            lv_obj_set_style_bg_color(scr, lv_color_hex(bgc), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
        }
        
        // Hide if not first
        if (page_count > 0 || !groups.empty()) {
            lv_obj_add_flag(scr, LV_OBJ_FLAG_HIDDEN);
        }
        
        page_ids.push_back(id);
        page_objs.push_back(scr);
        
        contents[content_count].start = content_start;
        contents[content_count].len = pclose - content_start;
        contents[content_count].parent = scr;
        content_count++;
        
        store_page(id.c_str(), scr);
        LOG_I(Log::UI, "page: %s (%d bytes content)", id.c_str(), (int)(pclose - content_start));
        
        page_count++;
        p = pclose + tagCloseLen(Layout::Page);
    }
    
    LOG_I(Log::UI, "render_internal: Pass 3 - children (%d pages)", content_count);
    // Pass 3: Parse children for all pages
    for (int i = 0; i < content_count; i++) {
        LOG_I(Log::UI, "  page[%d]: len=%d parent=%p", i, contents[i].len, contents[i].parent);
        parse_children(contents[i].start, contents[i].len, contents[i].parent);
    }
    
    LOG_I(Log::UI, "Done: %d elements, %d groups, %d standalone pages", 
             (int)elements.size(), (int)groups.size(), page_count);
    
    // Apply <ui default="/page"> — navigate to specified initial page
    if (!ui_default_page.empty()) {
        LOG_I(Log::UI, "Navigating to default page: %s", ui_default_page.c_str());
        applyZIndexOrdering();
        ui_show_page_internal(ui_default_page.c_str());
    }
    
    // Memory after UI render
    Serial.print("[Heap] After render: DRAM=");
    Serial.print(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Serial.print(" PSRAM=");
    Serial.println(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    return (int)elements.size();
}

void ui_clear_internal(void) {
    LOG_D(Log::UI, "clear: cleaning screen...");
    lv_obj_t* scr = get_screen();
    if (scr) {
        int child_cnt = lv_obj_get_child_cnt(scr);
        LOG_D(Log::UI, "clear: removing %d children", child_cnt);
        lv_obj_clean(scr);
        // Disable scroll to prevent jitter
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    // Clear image paths storage (must be done AFTER lv_obj_clean!)
    LOG_D(Log::UI, "clear: clearing %d icon paths, %d image paths", 
          (int)s_iconPaths.size(), (int)s_imagePaths.size());
    s_iconPaths.clear();
    s_imagePaths.clear();
    
    LOG_D(Log::UI, "clear: clearing elements vector...");
    elements.clear();
    s_deferredZIndex.clear();
    page_count = 0;
    current_page = 0;
    groups.clear();
    current_group = INVALID_INDEX;
    ui_default_page = "";
    LOG_D(Log::UI, "clear: done");
}
