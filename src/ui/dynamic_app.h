#pragma once
/**
 * DynamicApp — Loaded application state (RAII)
 *
 * Single owner of all per-app UI state. Destroyed on app switch,
 * all members auto-freed by destructors. No manual .clear() lists.
 */

#include "ui/ui_types.h"
#include <cstdint>
#include <unordered_map>

// Forward declarations
typedef struct _lv_obj_t lv_obj_t;

constexpr size_t MAX_SCREENS = 16;
constexpr int INVALID_INDEX = -1;

class DynamicApp {
public:
    DynamicApp() = default;
    ~DynamicApp();

    // Non-copyable, non-movable — owned exclusively by Core as a direct field
    DynamicApp(const DynamicApp&) = delete;
    DynamicApp& operator=(const DynamicApp&) = delete;
    DynamicApp(DynamicApp&&) = delete;
    DynamicApp& operator=(DynamicApp&&) = delete;

    // --- Lookups ---
    int        findPage(const char* id) const;
    lv_obj_t*  findElement(const char* id) const;
    
    // --- Mutations ---
    int  addElement(struct ElementDesc& d);
    int  addPage(const char* id, lv_obj_t* obj);
    void setZIndex(lv_obj_t* handle, int z);
    void applyZIndex();

    // Elements & pages
    MPArray<UI::Element> elements;
    P::Array<P::String>  page_ids;
    P::Array<lv_obj_t*>  page_objs;
    int                   page_count = 0;
    int                   current_page = 0;
    int                   current_group = INVALID_INDEX;
    P::Array<UI::PageGroup> groups;
    
    // Timers & styles
    P::Array<UI::Timer>  timers;
    P::Array<UI::Style>  styles;
    
    // Templates
    P::Map<P::String, P::String> templates;
    std::unordered_map<lv_obj_t*, int> deferredZIndex;
    
    // Script
    P::String script_code;
    P::String script_lang = "lua";
    
    // App metadata
    P::String app_version = "0.0";
    P::String app_os_requirement;
    P::String app_icon;
    P::String app_path;
    P::String ui_default_page;
    bool      app_readonly = false;
    
    // Resource path storage (LVGL needs valid pointers)
    P::Array<P::String> iconPaths;
    P::Array<P::String> imagePaths;
    
    // Keyboards
    lv_obj_t* keyboards[MAX_SCREENS] = {};
    
    // Runtime flags
    bool updating_from_binding = false;
    bool in_lvgl_callback = false;
};

