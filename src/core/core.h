/**
 * core/core.h - Central owner of UI state, Store, and rendering
 * 
 * Core owns DynamicApp (UI state) and Store.
 * Single global instance g_core defined in main.cpp.
 */

#ifndef UI_ENGINE_H
#define UI_ENGINE_H

#include "ui/ui_types.h"
#include "ui/dynamic_app.h"
#include "core/state_store.h"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <cstdint>

// Forward declaration for LVGL
typedef struct _lv_obj_t lv_obj_t;

// ============ UI ENGINE CLASS ============

class Core {
public:
    Core() = default;

    // Delete copy/move — Engine is unique
    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    
    // Lifecycle
    void initDynamicApp(const char* appPath);   // HTML app: placement new reset + set path
    void    resetUI();                              // Launcher/native: reset LVGL objects only
    int render(const char* html);

    // App state accessor — ref prevents assignment
    DynamicApp& app() { return m_app; }

    // State store accessor
    Store& store() { return m_store; }

    // Element access
    lv_obj_t* get(const char* id);
    void setText(const char* id, const char* text);
    void showPage(const char* id);
    bool hasPage(const char* id);  // Check if page exists
    const char* currentPageId() const;  // Current visible page ID
    
    // Keyboard
    bool isKeyboardVisible() const;  // Any keyboard shown?
    void dismissKeyboard();          // Hide all keyboards
    
    // Batch UI updates
    void freeze();      // Defer binding updates
    void unfreeze();    // Flush deferred updates
    
    // Head section getters
    int timersCount() const;
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

private:
    DynamicApp m_app{};
    Store      m_store{"State"};
}; // class Engine

/// Get currently focused textarea (set by focusInput, used by ui type)
lv_obj_t* getFocusedTextarea();

/// Get widget center in screen coordinates. Returns false if widget not found.
bool getWidgetCenter(const char* id, int& cx, int& cy);
bool triggerClick(const char* widgetId);   // Call widget's onclick handler
bool callFunction(const char* funcName);   // Call Lua function by name

namespace UI {
    /// Set focus to input widget (opens keyboard). Returns false if not found.
    bool focusInput(const char* id);

    /// Imperative attribute access
    bool setWidgetAttr(const char* id, const char* attr, const char* value);
    P::String getWidgetAttr(const char* id, const char* attr);
}

/// Global core instance — defined in main.cpp
extern Core g_core;

#endif // UI_ENGINE_H
