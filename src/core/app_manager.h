#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include <string>
#include <memory>
#include "utils/psram_alloc.h"
#include "core/sys_paths.h"
#include "ui/ui_html.h"
#include "ui/ui_launcher.h"
#include "core/script_engine.h"
#include "core/script_manager.h"
#include "core/native_app.h"
#include "core/yaml_config.h"

/**
 * AppManager - Manages app loading and launcher
 */
namespace App {

struct AppInfo {
    P::String name;
    P::String title;
    P::String category;       // "game", "tool", etc. for icon fallback
    P::String iconPath;       // LVGL path, e.g. SYS_LVGL_PREFIX SYS_APPS "myapp/icon.png"
    enum Source { HTML, Native } source = HTML;
    NativeApp* nativePtr = nullptr;
    
    // Pre-decoded icon in PSRAM (zero DRAM allocations at render time)
    lv_image_dsc_t iconDsc = {};   // LVGL image descriptor
    uint8_t* iconData = nullptr;   // Raw pixel buffer in PSRAM
    bool hasIcon = false;
    
    P::String path() const {
        char buf[64];
        snprintf(buf, sizeof(buf), SYS_APPS "%s/%s.bax", name.c_str(), name.c_str());
        return buf;
    }
};

class Manager {
public:
    static Manager& instance() {
        static Manager mgr;
        return mgr;
    }
    
    void init();
    bool launch(const P::String& name);
    void queueLaunch(const P::String& name) { m_pendingLaunch = name; }
    void returnToLauncher();
    void refreshApps();  // rescan + reload icons + re-render launcher if visible
    void processPendingLaunch();
    bool inLauncher() const { return m_inLauncher; }
    const std::vector<AppInfo>& apps() const { return m_apps; }
    
    YamlConfig systemConfig{"/system/config.yml", "SysConfig"};

private:
    Manager() = default;
    
    void mountLittleFS();
    void scanApps();
    void preloadIcons();
    void printAppList();
    void showLauncher();
    bool loadApp(const P::String& path);
    bool launchNative(NativeApp* app);
    
    std::vector<AppInfo> m_apps;
    std::unique_ptr<IScriptEngine> m_scriptEngine;
    std::unique_ptr<ScriptManager> m_scriptMgr;
    UI::Launcher m_launcher;
    
    bool m_inLauncher = true;
    P::String m_pendingLaunch;
    bool m_pendingReturnToLauncher = false;
    P::String m_currentApp;
    P::String m_currentAppTitle;
    NativeApp* m_currentNative = nullptr;
};

} // namespace App