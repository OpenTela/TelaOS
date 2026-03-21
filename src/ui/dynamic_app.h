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

    // --- Page accessors ---
    int            pageCount()   const { return page_count; }
    int            currentPage() const { return page_count > 0 ? current_page : INVALID_INDEX; }
    int            currentGroup() const { return current_group; }
    lv_obj_t*      pageObj(int i) const { return (i >= 0 && i < page_count) ? page_objs[i] : nullptr; }
    const P::String& pageId(int i) const { return page_ids[i]; }
    void           setCurrentPage(int idx)  { current_page  = idx; }
    void           setCurrentGroup(int idx) { current_group = idx; }
    void           pushPage(const char* id, lv_obj_t* obj) {
                       page_ids.push_back(id);
                       page_objs.push_back(obj);
                       page_count++;
                   }

    // --- Element accessors ---
    int            elementCount() const { return (int)elements.size(); }
    UI::Element*   elementAt(int i) const { return elements[i].get(); }

    // --- Keyboard accessors ---
    lv_obj_t*&     keyboard(int i)       { return keyboards[i]; }
    lv_obj_t*      keyboard(int i) const { return keyboards[i]; }

    // --- Group accessors ---
    int             groupCount()   const { return (int)groups.size(); }
    UI::PageGroup&  group(int i)         { return groups[i]; }
    UI::PageGroup&  addGroup()           { groups.push_back(UI::PageGroup{}); return groups.back(); }
    void            popGroup()           { groups.pop_back(); }
    int             findGroup(const char* id) const {
                        for (int i = 0; i < (int)groups.size(); i++)
                            if (groups[i].id == id) return i;
                        return INVALID_INDEX;
                    }

    // --- Script accessors ---
    const P::String& scriptCode() const { return script_code; }
    const P::String& scriptLang() const { return script_lang; }
    void setScriptCode(const P::String& code) { script_code = code; }
    void setScriptLang(const P::String& lang) { script_lang = lang; }

    // --- Timer accessors ---
    int               timerCount()             const { return (int)timers.size(); }
    const UI::Timer&  timer(int i)             const { return timers[i]; }
    void              addTimer(UI::Timer t)          { timers.push_back(std::move(t)); }

    // --- App metadata accessors ---
    const P::String& appVersion()       const { return app_version; }
    const P::String& appPath()          const { return app_path; }
    const P::String& appIcon()          const { return app_icon; }
    const P::String& appOsRequirement() const { return app_os_requirement; }
    const P::String& defaultPage()      const { return ui_default_page; }
    bool             appReadonly()      const { return app_readonly; }
    void setAppPath(const P::String& p)           { app_path = p; }
    void setAppVersion(const P::String& v)        { app_version = v; }
    void setAppIcon(const P::String& icon)        { app_icon = icon; }
    void setAppOsRequirement(const P::String& os) { app_os_requirement = os; }
    void setDefaultPage(const P::String& p)       { ui_default_page = p; }
    void setAppReadonly(bool v)                   { app_readonly = v; }

    // --- Resource path storage (LVGL needs stable pointers) ---
    const char* addIconPath(const P::String& p)  { iconPaths.push_back(p);  return iconPaths.back().c_str(); }
    const char* addImagePath(const P::String& p) { imagePaths.push_back(p); return imagePaths.back().c_str(); }

    // Elements & pages (raw access needed by UI layer)
    MPArray<UI::Element> elements;

    // --- Template accessors ---
    int  templateCount() const { return (int)templates.size(); }
    bool hasTemplates()  const { return !templates.empty(); }
    void setTemplate(const P::String& name, const P::String& body) { templates[name] = body; }
    const P::String* findTemplate(const P::String& name) const {
        auto it = templates.find(name);
        return it != templates.end() ? &it->second : nullptr;
    }

    // Styles (raw access needed by UI layer)
    P::Array<UI::Style>  styles;

    // Keyboards (raw access needed by UI layer)
    lv_obj_t* keyboards[MAX_SCREENS] = {};

    // --- Runtime flags ---
    bool isUpdatingFromBinding() const  { return updating_from_binding; }
    bool isInLvglCallback()      const  { return in_lvgl_callback; }
    void beginBinding()                 { updating_from_binding = true; }
    void endBinding()                   { updating_from_binding = false; }
    void beginCallback()                { in_lvgl_callback = true; }
    void endCallback()                  { in_lvgl_callback = false; }

private:
    // Script
    P::String script_code;
    P::String script_lang    = "lua";
    // App metadata
    P::String app_version    = "0.0";
    P::String app_os_requirement;
    P::String app_icon;
    P::String app_path;
    P::String ui_default_page;
    bool      app_readonly   = false;
    // Resource paths
    P::Array<P::String> iconPaths;
    P::Array<P::String> imagePaths;
    // Timers
    P::Array<UI::Timer> timers;
    // Runtime flags
    bool updating_from_binding = false;
    bool in_lvgl_callback      = false;
    // Templates & z-index
    P::Map<P::String, P::String> templates;
    std::unordered_map<lv_obj_t*, int> deferredZIndex;
    // Pages
    P::Array<UI::PageGroup> groups;
    P::Array<P::String>  page_ids;
    P::Array<lv_obj_t*>  page_objs;
    int                  page_count    = 0;
    int                  current_page  = 0;
    int                  current_group = INVALID_INDEX;
};

