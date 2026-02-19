/**
 * ui_engine.h - HTML UI engine for LVGL (C++17)
 * 
 * Main class for parsing and rendering HTML-like UI markup.
 * Uses singleton pattern for global access.
 */

#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include "ui/ui_types.h"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <cstdint>

// Forward declaration for LVGL
typedef struct _lv_obj_t lv_obj_t;

namespace UI {

// ============ DATA STRUCTURES ============
// Timer, Style, Element определены в ui_types.h

enum class IndicatorType { Scrollbar = 0, Dots = 1, None = 2 };

struct PageGroup {
    P::String id;
    P::String default_page;
    Orientation orientation = Orientation::Horizontal;
    IndicatorType indicator = IndicatorType::Scrollbar;
    lv_obj_t *tileview = nullptr;
    lv_obj_t *indicator_obj = nullptr;
    lv_obj_t *screen = nullptr;          // parent screen, set by create()
    P::Array<P::String> page_ids;
    P::Array<lv_obj_t*> page_objs;
    int current_page_idx = 0;
    
    bool isHorizontal() const { return orientation == Orientation::Horizontal; }
    
    /// Create tileview on parent screen (reads indicator field for scrollbar mode)
    void create(lv_obj_t* parent);
    
    /// Add a tile (page) to the group, returns tile lv_obj
    lv_obj_t* addTile(const P::String& pageId);
    
    /// Finalize after all tiles added: create indicators, bind events
    void finalize(int grpIdx);
    
    /// Update indicator to highlight active page
    void updateIndicator(int activeIdx);
    
    /// Hide tileview + indicator
    void hide();
};

// ============ UI ENGINE CLASS ============

class Engine {
public:
    // Singleton access
    static Engine& instance();
    
    // Delete copy/move
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    
    // Lifecycle
    void init();
    int render(const char* html);
    void clear();
    
    // Element access
    lv_obj_t* get(const char* id);
    void setText(const char* id, const char* text);
    void showPage(const char* id);
    bool hasPage(const char* id);  // Check if page exists
    const char* currentPageId() const;  // Current visible page ID
    
    // Head section getters
    int timerCount() const;
    int timerInterval(int i) const;
    const char* timerCallback(int i) const;
    
    const char* scriptCode() const;
    const char* scriptLang() const;
    
    
    int stateCount() const;
    const char* stateVarName(int i) const;
    const char* stateVarType(int i) const;
    const char* stateVarDefault(int i) const;
    
    // Handler types
    using OnClickHandler = void (*)(const char* func_name);
    using OnTapHandler = void (*)(const char* func_name, int x, int y);
    using OnHoldHandler = void (*)(const char* func_name);
    using OnHoldXYHandler = void (*)(const char* func_name, int x, int y);
    using StateChangeHandler = void (*)(const char* var_name, const char* value);
    
    // Handler setters
    void setOnClickHandler(OnClickHandler handler);
    void setOnTapHandler(OnTapHandler handler);
    void setOnHoldHandler(OnHoldHandler handler);
    void setOnHoldXYHandler(OnHoldXYHandler handler);
    void setStateChangeHandler(StateChangeHandler handler);
    
    // Widget sync
    void syncWidgetValues();
    
    // Canvas API
    bool canvasClear(const char* id, uint32_t color);
    bool canvasRect(const char* id, int x, int y, int w, int h, uint32_t color);
    bool canvasPixel(const char* id, int x, int y, uint32_t color);
    bool canvasCircle(const char* id, int cx, int cy, int r, uint32_t color);
    bool canvasLine(const char* id, int x1, int y1, int x2, int y2, uint32_t color, int thickness = 1);
    bool canvasRefresh(const char* id);
    
    // Version
    static constexpr const char* version() { return "5.2.0"; }
    const char* appVersion() const;
    const char* appOsRequirement() const;
    const char* appIcon() const;
    bool appReadonly() const;
    
    // App resources path
    void setAppPath(const char* path);
    const char* appPath() const;

private:
    Engine() = default;
}; // class Engine

/// Get currently focused textarea (set by focusInput, used by ui type)
lv_obj_t* getFocusedTextarea();

/// Get widget center in screen coordinates. Returns false if widget not found.
bool getWidgetCenter(const char* id, int& cx, int& cy);
bool triggerClick(const char* widgetId);   // Call widget's onclick handler
bool callFunction(const char* funcName);   // Call Lua function by name

/// Set focus to input widget (opens keyboard). Returns false if not found.
bool focusInput(const char* id);

/// Imperative attribute access
bool setWidgetAttr(const char* id, const char* attr, const char* value);
P::String getWidgetAttr(const char* id, const char* attr);

} // namespace UI

#endif // UI_ENGINE_H
