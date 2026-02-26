/**
 * Evolution OS - App Manager
 * Adapted from ESP-IDF version for PlatformIO/Arduino
 */

#include "core/app_manager.h"
#include "widgets/widget_common.h"
#include "hal/display_hal.h"
#include "ui/ui_shade.h"
#ifndef NO_BLE
#include "ble/ble_bridge.h"
#endif
#include "utils/font.h"
#include "utils/psram_alloc.h"
#include "engines/lua/lua_engine.h"
#include "engines/lua/lua_system.h"
#include "engines/bf/bf_engine.h"
#include "utils/log_config.h"
#include "_generated_icons.h"
#include "esp_heap_caps.h"
#include <Arduino.h>
#include <lvgl.h>
#include <LittleFS.h>
#include <sys/stat.h>
#include <cstring>

// lodepng is compiled into LVGL — declare extern to use directly
extern "C" {
    unsigned lodepng_decode32(unsigned char** out, unsigned* w, unsigned* h,
                              const unsigned char* in, size_t insize);
}
#include <algorithm>
#include <cctype>
#include <functional>

static const char* TAG = "AppMgr";

// ============================================
// State Machine
// ============================================
enum class AppState {
    NONE,
    LAUNCHER,
    APP_RUNNING,
    TRANSITIONING
};

static AppState s_appState = AppState::NONE;

static const char* stateToStr(AppState s) {
    switch (s) {
        case AppState::NONE: return "NONE";
        case AppState::LAUNCHER: return "LAUNCHER";
        case AppState::APP_RUNNING: return "APP_RUNNING";
        case AppState::TRANSITIONING: return "TRANSITIONING";
        default: return "?";
    }
}

// ============================================
// Screen cleanup - removes all event callbacks
// ============================================
static void cleanupScreen() {
    lv_obj_t* scr = lv_screen_active();
    if (!scr) return;
    
    LOG_D(Log::APP, "Cleaning up screen callbacks...");
    
    uint32_t cnt = lv_obj_get_event_count(scr);
    LOG_D(Log::APP, "Screen has %d event callbacks", (int)cnt);
    
    for (int32_t i = (int32_t)cnt - 1; i >= 0; i--) {
        lv_obj_remove_event(scr, (uint32_t)i);
    }
    
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_EVENT_BUBBLE);
    
    LOG_D(Log::APP, "Screen cleanup complete");
}

// ============================================
// Extract metadata from app file (first 512 bytes)
// Returns: title, icon, version from <app title="..." icon="..." version="...">
// ============================================
struct AppMeta {
    P::String title;
    P::String icon;
    P::String version;
};

static AppMeta extractMeta(const P::String& filepath, const P::String& defaultName) {
    AppMeta meta;
    meta.title = defaultName;
    if (!meta.title.empty()) {
        meta.title[0] = toupper(meta.title[0]);
    }
    
    File f = LittleFS.open(filepath.c_str(), "r");
    if (!f) return meta;
    
    char buf[512];
    size_t len = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    buf[len] = '\0';
    
    const char* appTag = strstr(buf, "<app");
    if (!appTag) return meta;
    
    const char* tagEnd = strchr(appTag, '>');
    if (!tagEnd) return meta;
    
    // Helper lambda to extract attribute value
    auto getAttr = [&](const char* name) -> P::String {
        char search[32];
        snprintf(search, sizeof(search), "%s=\"", name);
        const char* attr = strstr(appTag + 4, search);
        if (attr && attr < tagEnd) {
            attr += strlen(search);
            const char* end = strchr(attr, '"');
            if (end && end <= tagEnd) {
                return P::String(attr, end - attr);
            }
        }
        return "";
    };
    
    // Extract title
    P::String title = getAttr("title");
    if (!title.empty()) {
        meta.title = title;
    }
    
    // Extract icon (support system: prefix)
    P::String icon = getAttr("icon");
    if (!icon.empty()) {
        if (icon.size() > 7 && strncmp(icon.c_str(), "system:", 7) == 0) {
            // system:puzzle-game -> C:/system/resources/icons/puzzle-game.png
            meta.icon = P::String(SYS_LVGL_PREFIX SYS_ICONS) + icon.substr(7) + ".png";
        } else if (icon[0] == '/') {
            // Absolute path -> add C: prefix
            meta.icon = P::String(SYS_LVGL_PREFIX) + icon;
        } else {
            meta.icon = icon;
        }
    }
    
    // Extract version
    meta.version = getAttr("version");
    
    return meta;
}

// ============================================
// Extract category from app file
// ============================================
static P::String extractCategory(const P::String& filepath) {
    File f = LittleFS.open(filepath.c_str(), "r");
    if (!f) return "";
    
    char buf[256];
    size_t len = f.readBytes(buf, sizeof(buf) - 1);
    f.close();
    buf[len] = '\0';
    
    const char* appTag = strstr(buf, "<app");
    if (appTag) {
        const char* tagEnd = strchr(appTag, '>');
        if (tagEnd) {
            const char* catAttr = strstr(appTag + 4, "category=\"");
            if (catAttr && catAttr < tagEnd) {
                catAttr += 10;
                const char* end = strchr(catAttr, '"');
                if (end && end <= tagEnd) {
                    return P::String(catAttr, end - catAttr);
                }
            }
        }
    }
    return "";
}

static lv_obj_t* s_confirmOverlay = nullptr;

static void hideCloseConfirm() {
    if (s_confirmOverlay) {
        lv_obj_delete(s_confirmOverlay);
        s_confirmOverlay = nullptr;
    }
}

static void showCloseConfirm(const char* appTitle) {
    hideCloseConfirm();  // safety

    // Full-screen semi-transparent blocker on layer_top
    s_confirmOverlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_confirmOverlay);
    lv_obj_set_size(s_confirmOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_confirmOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_confirmOverlay, LV_OPA_50, 0);
    lv_obj_add_flag(s_confirmOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_confirmOverlay, LV_OBJ_FLAG_SCROLLABLE);

    // Tap outside → cancel
    lv_obj_add_event_cb(s_confirmOverlay, [](lv_event_t* e) {
        lv_obj_t* target = (lv_obj_t*)lv_event_get_target(e);
        if (target == s_confirmOverlay) hideCloseConfirm();
    }, LV_EVENT_CLICKED, nullptr);

    // Center panel
    lv_obj_t* panel = lv_obj_create(s_confirmOverlay);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 280, 140);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    // Title: "Close <AppName>?"
    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "Close %s?", appTitle);

    lv_obj_t* label = lv_label_create(panel);
    lv_label_set_text(label, titleBuf);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, UI::Font::get(UI::Font::SMALL), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 8);

    // Yes button
    lv_obj_t* btnYes = lv_btn_create(panel);
    lv_obj_set_size(btnYes, 110, 44);
    lv_obj_align(btnYes, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    lv_obj_set_style_bg_color(btnYes, lv_color_hex(0xC62828), 0);
    lv_obj_set_style_radius(btnYes, 8, 0);

    lv_obj_t* lblYes = lv_label_create(btnYes);
    lv_label_set_text(lblYes, "Yes");
    lv_obj_set_style_text_color(lblYes, lv_color_white(), 0);
    lv_obj_set_style_text_font(lblYes, UI::Font::get(UI::Font::SMALL), 0);
    lv_obj_center(lblYes);

    lv_obj_add_event_cb(btnYes, [](lv_event_t* e) {
        hideCloseConfirm();
        App::Manager::instance().returnToLauncher();
    }, LV_EVENT_CLICKED, nullptr);

    // No button
    lv_obj_t* btnNo = lv_btn_create(panel);
    lv_obj_set_size(btnNo, 110, 44);
    lv_obj_align(btnNo, LV_ALIGN_BOTTOM_RIGHT, -8, -4);
    lv_obj_set_style_bg_color(btnNo, lv_color_hex(0x424242), 0);
    lv_obj_set_style_radius(btnNo, 8, 0);

    lv_obj_t* lblNo = lv_label_create(btnNo);
    lv_label_set_text(lblNo, "No");
    lv_obj_set_style_text_color(lblNo, lv_color_white(), 0);
    lv_obj_set_style_text_font(lblNo, UI::Font::get(UI::Font::SMALL), 0);
    lv_obj_center(lblNo);

    lv_obj_add_event_cb(btnNo, [](lv_event_t* e) {
        hideCloseConfirm();
    }, LV_EVENT_CLICKED, nullptr);

    LOG_D(Log::UI, "Close confirm shown for: %s", appTitle);
}

namespace App {

// ============================================
// Manager
// ============================================
void Manager::init() {
    LOG_I(Log::APP, "=== Evolution OS ===");
    
    mountLittleFS();

    // System config
    systemConfig.define("display.brightness",  VarType::Int,  255);
    systemConfig.define("power.auto_sleep",    VarType::Bool, true);
    systemConfig.define("power.nudge_timeout", VarType::Int,  30);
    systemConfig.define("power.dim_timeout",   VarType::Int,  45);
    systemConfig.define("power.sleep_timeout", VarType::Int,  60);
    systemConfig.define("bluetooth.enabled", VarType::Bool, false);

    if (systemConfig.load()) {
        LOG_I(Log::APP, "Config loaded: /system/config.yml");
    } else {
        LOG_I(Log::APP, "Config not found, saving defaults");
        systemConfig.save();
    }

    Shade::applyConfig();

    scanApps();
#if defined(PRELOAD_ICONS_TO_PSRAM) && !defined(NO_PNG_ICONS)
    preloadIcons();
#endif
    printAppList();
    showLauncher();
    
    LOG_I(Log::APP, "Ready!");
}

void Manager::mountLittleFS() {
    LOG_I(Log::APP, "Mounting LittleFS...");
    
    if (!LittleFS.begin(true, "/littlefs", 10, "storage")) {
        LOG_E(Log::APP, "LittleFS mount failed!");
        return;
    }
    
    size_t total = LittleFS.totalBytes();
    size_t used = LittleFS.usedBytes();
    LOG_I(Log::APP, "LittleFS: %d/%d bytes", (int)used, (int)total);
}

void Manager::scanApps() {
    m_apps.clear();
    
    // Scan .bax apps from filesystem
    File appsDir = LittleFS.open(SYS_APPS);
    if (!appsDir || !appsDir.isDirectory()) {
        LOG_W(Log::APP, SYS_APPS " not found");
    } else {
        File entry;
        while ((entry = appsDir.openNextFile())) {
            if (!entry.isDirectory()) continue;
            
            P::String name = entry.name();
            if (name.find(SYS_APPS) == 0) {
                name = name.substr(sizeof(SYS_APPS) - 1);
            }
            
            char pathBuf[64];
            snprintf(pathBuf, sizeof(pathBuf), SYS_APPS "%s/%s.bax", name.c_str(), name.c_str());
            if (!LittleFS.exists(pathBuf)) continue;
            
            AppInfo app;
            app.name = name;
            
            AppMeta meta = extractMeta(pathBuf, name);
            app.title = meta.title;
            app.category = extractCategory(pathBuf);
            app.source = AppInfo::HTML;
            
            if (!meta.icon.empty()) {
                app.iconPath = meta.icon;
            } else {
                // Check for icon.png by listing app dir (silent, no VFS open() errors)
                bool hasIcon = false;
                char dirBuf[64];
                snprintf(dirBuf, sizeof(dirBuf), SYS_APPS "%s", name.c_str());
                File appDir = LittleFS.open(dirBuf);
                if (appDir && appDir.isDirectory()) {
                    File f;
                    while ((f = appDir.openNextFile())) {
                        P::String fname = f.name();
                        // f.name() may return full path or just filename
                        auto slash = fname.rfind('/');
                        if (slash != P::String::npos) fname = fname.substr(slash + 1);
                        if (fname == "icon.png") {
                            hasIcon = true;
                            f.close();
                            break;
                        }
                        f.close();
                    }
                }
                if (hasIcon) {
                    snprintf(pathBuf, sizeof(pathBuf), SYS_LVGL_PREFIX SYS_APPS "%s/icon.png", name.c_str());
                    app.iconPath = pathBuf;
                }
                // else iconPath stays empty → launcher uses category/builtin fallback
            }
            
            m_apps.push_back(app);
            LOG_D(Log::APP, "Found: %s (%s)", app.name.c_str(), app.title.c_str());
        }
    }
    
    // Add native apps from registry
    for (auto* native : NativeApp::all()) {
        AppInfo app;
        app.name = native->name();
        app.title = native->title();
        app.source = AppInfo::Native;
        app.nativePtr = native;
        m_apps.push_back(app);
        LOG_D(Log::APP, "Native: %s (%s)", app.name.c_str(), app.title.c_str());
    }
    
    // Sort by name
    std::sort(m_apps.begin(), m_apps.end(), [](const AppInfo& a, const AppInfo& b) {
        return a.name < b.name;
    });
    
    LOG_I(Log::APP, "Total: %d apps", (int)m_apps.size());
}

void Manager::printAppList() {
    log_printf("\n========== APPS (%d) ==========\n", (int)m_apps.size());
    for (const auto& app : m_apps) {
        // Icon source tag
        const char* iconStr = "[none]";
        char iconBuf[64] = {};
        
        if (app.source == AppInfo::Native) {
            iconStr = "[native]";
        } else if (!app.iconPath.empty()) {
            P::String path = app.iconPath;
            P::String fname;
            
            if (path.find(SYS_LVGL_PREFIX SYS_ICONS) == 0) {
                fname = path.substr(sizeof(SYS_LVGL_PREFIX SYS_ICONS) - 1);
            } else {
                auto slash = path.rfind('/');
                fname = (slash != P::String::npos) 
                    ? path.substr(slash + 1) : path;
            }
            
            // Determine source: embed (flash) / stale (fs, icon changed) / psram / fs
            const char* src = "fs";
            
#ifdef USE_BUILTIN_ICONS
            {
                const BuiltinIcon* entry = findBuiltinEntry(app.name.c_str());
                if (!entry && path.find(SYS_LVGL_PREFIX SYS_ICONS) == 0) {
                    P::String iconName = path.substr(sizeof(SYS_LVGL_PREFIX SYS_ICONS) - 1);
                    size_t dot = iconName.rfind('.');
                    if (dot != P::String::npos) iconName = iconName.substr(0, dot);
                    entry = findSystemEntry(iconName.c_str());
                }
                if (entry) {
                    // Check staleness
                    P::String fsPath = path;
                    if (fsPath.size() > 2 && fsPath[0] == 'C' && fsPath[1] == ':')
                        fsPath = fsPath.substr(2);
                    bool stale = false;
                    if (LittleFS.exists(fsPath.c_str())) {
                        File f = LittleFS.open(fsPath.c_str(), "r");
                        if (f) { 
                            uint32_t fsSize = (uint32_t)f.size();
                            stale = (fsSize != entry->png_size); 
                            if (stale) {
                                LOG_I(Log::APP, "  stale: %s fs=%u embed=%u", 
                                      app.name.c_str(), fsSize, entry->png_size);
                            }
                            f.close(); 
                        }
                    }
                    src = stale ? "stale->fs" : "embed";
                } else {
                    LOG_D(Log::APP, "  no builtin: %s", app.name.c_str());
                }
            }
#endif

#if defined(PRELOAD_ICONS_TO_PSRAM) && !defined(NO_PNG_ICONS)
            if (app.hasIcon && strcmp(src, "fs") == 0) {
                src = "psram";
            }
#endif
            
            snprintf(iconBuf, sizeof(iconBuf), "%s [%s]", fname.c_str(), src);
            iconStr = iconBuf;
        }
        
        log_printf("  %-16s \"%s\" %s\n",
            app.name.c_str(),
            app.title.empty() ? "" : app.title.c_str(),
            iconStr);
    }
    log_printf("================================\n");
}

void Manager::preloadIcons() {
    LOG_I(Log::APP, "Preloading icons to PSRAM...");
    
    int loaded = 0;
    int skipped = 0;
    int embedded = 0;
    size_t totalBytes = 0;
    
    for (auto& app : m_apps) {
        // Use iconPath from app metadata (may be system: resolved or custom path)
        // Convert LVGL path (C:/...) to LittleFS path (/...)
        P::String fsPath = app.iconPath;
        if (fsPath.size() > 2 && fsPath[0] == 'C' && fsPath[1] == ':') {
            fsPath = fsPath.substr(2);
        }
        
        // --- Hybrid: skip if embedded icon is still fresh ---
#ifdef USE_BUILTIN_ICONS
        {
            // Check app icon by name
            const BuiltinIcon* entry = findBuiltinEntry(app.name.c_str());
            
            // Check system icon if app icon not found
            if (!entry && !app.iconPath.empty()) {
                const char* sysPrefix = SYS_LVGL_PREFIX SYS_ICONS;
                if (strncmp(app.iconPath.c_str(), sysPrefix, strlen(sysPrefix)) == 0) {
                    const char* nameStart = app.iconPath.c_str() + strlen(sysPrefix);
                    P::String iconName(nameStart);
                    size_t dotPos = iconName.rfind('.');
                    if (dotPos != P::String::npos) iconName = iconName.substr(0, dotPos);
                    entry = findSystemEntry(iconName.c_str());
                }
            }
            
            if (entry) {
                // Compare FS file size with build-time png_size
                bool stale = false;
                if (!fsPath.empty() && LittleFS.exists(fsPath.c_str())) {
                    File f = LittleFS.open(fsPath.c_str(), "r");
                    if (f) {
                        stale = ((uint32_t)f.size() != entry->png_size);
                        f.close();
                    }
                }
                if (!stale) {
                    // Embedded is up-to-date — skip PSRAM decode
                    embedded++;
                    continue;
                }
                LOG_I(Log::APP, "Icon changed on FS: %s (decode from disk)", app.name.c_str());
            }
        }
#endif
        
        if (!LittleFS.exists(fsPath.c_str())) { skipped++; continue; }
        
        File f = LittleFS.open(fsPath.c_str(), "r");
        if (!f) continue;
        
        size_t fileSize = f.size();
        uint8_t* pngData = (uint8_t*)ps_malloc(fileSize);
        if (!pngData) {
            f.close();
            continue;
        }
        
        f.read(pngData, fileSize);
        f.close();
        
        unsigned char* pixels = nullptr;
        unsigned w, h;
        unsigned error = lodepng_decode32(&pixels, &w, &h, pngData, fileSize);
        free(pngData);
        
        if (error || !pixels) {
            continue;
        }
        
        size_t pixelCount = w * h;
        size_t dataSize = pixelCount * 2;
        uint8_t* rgb565 = (uint8_t*)ps_malloc(dataSize);
        
        if (!rgb565) {
            free(pixels);
            continue;
        }
        
        uint16_t* dst = (uint16_t*)rgb565;
        uint8_t* src = pixels;
        for (size_t i = 0; i < pixelCount; i++) {
            uint8_t r = src[0];
            uint8_t g = src[1];
            uint8_t b = src[2];
            src += 4;
            
            uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            dst[i] = c;
        }
        free(pixels);
        
        app.iconData = rgb565;
        app.iconDsc.header.w = w;
        app.iconDsc.header.h = h;
        app.iconDsc.header.cf = LV_COLOR_FORMAT_RGB565;
        app.iconDsc.data = rgb565;
        app.iconDsc.data_size = dataSize;
        app.hasIcon = true;
        
        loaded++;
        totalBytes += dataSize;
    }
    
    LOG_I(Log::APP, "Icons: %d loaded (%d KB PSRAM), %d embedded (flash), %d skipped", 
           loaded, (int)(totalBytes / 1024), embedded, skipped);
}


void Manager::showLauncher() {
    LOG_I(Log::APP, "State: %s -> TRANSITIONING -> LAUNCHER", stateToStr(s_appState));
    s_appState = AppState::TRANSITIONING;
    
    m_currentApp.clear();
    m_currentAppTitle.clear();
    m_inLauncher = true;
    
    if (m_currentNative) {
        m_currentNative->onDestroy();
        m_currentNative = nullptr;
    }
    
    if (m_scriptMgr) {
        m_scriptMgr->shutdown();
        m_scriptMgr.reset();
    }
    if (m_scriptEngine) {
        m_scriptEngine.reset();
    }
    
    display_lock();
    
    WidgetCallbacks::cleanup();
    cleanupScreen();
    ui_engine().clear();
    lv_image_cache_drop(NULL);
    
    LOG_I(Log::APP, "Heap before launcher: %d bytes", (int)ESP.getFreeHeap());
    
    // Restore display buffer to boot level — launcher needs smooth swipe rendering
    int bootLines = display_get_boot_lines();
    if (display_get_buffer_lines() < bootLines) {
        display_set_buffer((DisplayBuffer)bootLines);
    }
    
    // Convert AppInfo to LauncherAppInfo
    std::vector<UI::LauncherAppInfo> launcherApps;
    launcherApps.reserve(m_apps.size());
    for (const auto& app : m_apps) {
        UI::LauncherAppInfo info;
        info.name = app.name;
        info.title = app.title;
        info.category = app.category;
        info.iconPath = app.iconPath;
        info.iconDsc = app.hasIcon ? &app.iconDsc : nullptr;
        info.hasIcon = app.hasIcon;
        launcherApps.push_back(info);
    }
    
    m_launcher.show(launcherApps);
    
    display_unlock();
    
    m_inLauncher = true;
    s_appState = AppState::LAUNCHER;
    LOG_I(Log::APP, "State: LAUNCHER | %d apps", (int)m_apps.size());
}

void Manager::refreshApps() {
    LOG_I(Log::APP, "Refreshing app list...");
    scanApps();
#if defined(PRELOAD_ICONS_TO_PSRAM) && !defined(NO_PNG_ICONS)
    preloadIcons();
#endif
    if (m_inLauncher) {
        showLauncher();
    }
}


bool Manager::launch(const P::String& name) {
    for (auto& app : m_apps) {
        if (app.name == name) {
            m_currentAppTitle = app.title.empty() ? app.name : app.title;
            if (app.source == AppInfo::Native)
                return launchNative(app.nativePtr);
            return loadApp(app.path());
        }
    }
    LOG_E(Log::APP, "App not found: %s", name.c_str());
    return false;
}

bool Manager::launchNative(NativeApp* app) {
    if (!app) return false;
    
    LOG_I(Log::APP, "Launching native: %s", app->name());
    s_appState = AppState::TRANSITIONING;
    
    display_lock();
    
    m_launcher.cleanup();
    WidgetCallbacks::cleanup();
    cleanupScreen();
    ui_engine().clear();
    lv_image_cache_drop(NULL);
    
    LOG_I(Log::APP, "Heap after cleanup: %d bytes", (int)ESP.getFreeHeap());
    
    display_unlock();
    
    m_currentNative = app;
    m_inLauncher = false;
    
    app->onCreate();
    
    s_appState = AppState::APP_RUNNING;
    LOG_I(Log::APP, "State: APP_RUNNING | Native app");
    return true;
}

bool Manager::loadApp(const P::String& path) {
    LOG_I(Log::APP, "Loading: %s", path.c_str());
    s_appState = AppState::TRANSITIONING;
    
    m_launcher.cleanup();
    
    if (m_scriptMgr) {
        m_scriptMgr->shutdown();
        m_scriptMgr.reset();
    }
    if (m_scriptEngine) {
        m_scriptEngine.reset();
    }
    
    cleanupScreen();
    
    ui_engine().clear();
    lv_image_cache_drop(NULL);
    
    LOG_I(Log::APP, "Heap after cleanup: %d bytes", (int)ESP.getFreeHeap());
    
    display_unlock();
    
    File f = LittleFS.open(path.c_str(), "r");
    if (!f) {
        LOG_E(Log::APP, "Failed to open: %s", path.c_str());
        s_appState = AppState::LAUNCHER;
        return false;
    }
    
    size_t size = f.size();
    P::String html(size, '\0');
    f.readBytes(&html[0], size);
    f.close();
    
    display_lock();
    
    P::String appDir(path.data(), path.rfind('/'));
    ui_engine().setAppPath(appDir.c_str());
    
    int count = ui_engine().render(html.c_str());
    LOG_D(Log::APP, "Rendered %d elements", count);
    
    static int16_t s_appTouchStartX = -1;
    static int16_t s_appTouchStartY = -1;
    
    lv_obj_t* scr = lv_screen_active();
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    
    std::function<void(lv_obj_t*)> addBubbling = [&](lv_obj_t* obj) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
        uint32_t cnt = lv_obj_get_child_count(obj);
        for (uint32_t i = 0; i < cnt; i++) {
            addBubbling(lv_obj_get_child(obj, i));
        }
    };
    addBubbling(scr);
    
    lv_obj_add_event_cb(scr, [](lv_event_t* e) {
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            s_appTouchStartX = p.x;
            s_appTouchStartY = p.y;
        }
    }, LV_EVENT_PRESSED, nullptr);
    
    lv_obj_add_event_cb(scr, [](lv_event_t* e) {
        if (s_appTouchStartY < 0) return;
        
        if (s_appState == AppState::TRANSITIONING) {
            s_appTouchStartY = -1;
            return;
        }
        
        lv_indev_t* indev = lv_indev_active();
        if (!indev) {
            s_appTouchStartY = -1;
            return;
        }
        
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        
        int dy = p.y - s_appTouchStartY;
        int dx = abs(p.x - s_appTouchStartX);
        
        const int EDGE_ZONE_PCT = 18;
        const int MIN_SWIPE_PCT = 50;      // definite close (was 60 — too far)
        const int CONFIRM_SWIPE_PCT = 25;  // confirm dialog (was 40 — too hard to trigger)
        const int MAX_DRIFT_PCT = 35;      // clean vertical threshold (was 25 — too strict)
        const int MAX_DRIFT_SLOPPY_PCT = 50; // sloppy but intentional (was 40)
        
        int startYPct = s_appTouchStartY * 100 / SCREEN_HEIGHT;
        int dyPct = abs(dy) * 100 / SCREEN_HEIGHT;
        int dxPct = dx * 100 / SCREEN_WIDTH;
        
        bool vertical = dxPct < MAX_DRIFT_PCT;
        bool sloppyVertical = dxPct < MAX_DRIFT_SLOPPY_PCT;
        
        bool fromTop = startYPct < EDGE_ZONE_PCT;
        bool swipedDown = dy > 0 && dyPct >= MIN_SWIPE_PCT;
        
        bool fromBottom = startYPct > (100 - EDGE_ZONE_PCT);
        bool swipedUp = dy < 0 && dyPct >= MIN_SWIPE_PCT;
        
        // --- Definite close: long + vertical ---
        if ((fromTop && swipedDown && vertical) || 
            (fromBottom && swipedUp && vertical)) {
            const char* gesture = fromTop ? "swipe-down-from-top" : "swipe-up-from-bottom";
            LOG_D(Log::APP, "Exit! %s startY=%d%% dy=%d%% dx=%d%%", 
                     gesture, startYPct, dyPct, dxPct);
            s_appTouchStartY = -1;
            Manager::instance().returnToLauncher();
            return;
        }
        
        // --- Ambiguous close: confirm dialog ---
        bool fromEdgeDown = fromTop && dy > 0;
        bool fromEdgeUp   = fromBottom && dy < 0;
        bool fromEdge     = fromEdgeDown || fromEdgeUp;
        bool ambiguousLength = fromEdge && dyPct >= CONFIRM_SWIPE_PCT && dyPct < MIN_SWIPE_PCT && vertical;
        bool ambiguousAngle  = fromEdge && dyPct >= CONFIRM_SWIPE_PCT && !vertical && sloppyVertical;
        
        if (ambiguousLength || ambiguousAngle) {
            LOG_D(Log::APP, "Confirm? startY=%d%% dy=%d%% dx=%d%%", startYPct, dyPct, dxPct);
            s_appTouchStartY = -1;
            showCloseConfirm(Manager::instance().m_currentAppTitle.c_str());
            return;
        }
        
        // Short swipe from top → open shade
        const int SHADE_SWIPE_PCT = 12;
        if (fromTop && dy > 0 && dyPct >= SHADE_SWIPE_PCT && dyPct < CONFIRM_SWIPE_PCT && vertical) {
            LOG_D(Log::APP, "Shade! startY=%d%% dy=%d%%", startYPct, dyPct);
            s_appTouchStartY = -1;
            Shade::open();
            return;
        }
        
        if (fromTop || fromBottom) {
            LOG_D(Log::APP, "No exit: startY=%d%% dy=%d%% dx=%d%% (need %d%%)",
                     startYPct, dyPct, dxPct, MIN_SWIPE_PCT);
        }
        s_appTouchStartY = -1;
    }, LV_EVENT_RELEASED, nullptr);
    
    display_unlock();
    
    const char* scriptCode = ui_engine().scriptCode();
    if (scriptCode && scriptCode[0]) {
        const char* lang = ui_engine().scriptLang();
        
        if (lang && strcmp(lang, "brainfuck") == 0) {
            LOG_I(Log::APP, "Init BfEngine...");
            m_scriptEngine = std::make_unique<BfEngine>();
        } else {
            LOG_D(Log::APP, "Init LuaEngine...");
            m_scriptEngine = std::make_unique<LuaEngine>();
            LuaSystem::setHomeCallback([]() {
                Manager::instance().m_pendingReturnToLauncher = true;
            });
        }
        
        m_scriptMgr = std::make_unique<ScriptManager>();
        if (!m_scriptMgr->init(m_scriptEngine.get())) {
            LOG_E(Log::APP, "ScriptManager init failed");
        }
    }
    
    m_inLauncher = false;
    m_currentApp = path;
    s_appState = AppState::APP_RUNNING;
    LOG_I(Log::APP, "State: APP_RUNNING | App loaded!");
    
    uint32_t dramFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint32_t psramUsed = psramTotal - psramFree;
    
    Serial.println("========== APP STATUS ==========");
#ifndef NO_BLE
    Serial.printf("[Status] BLE: %s\n", BLEBridge::isInitialized() ? "ON" : "OFF");
#else
    Serial.println("[Status] BLE: DISABLED");
#endif
    Serial.printf("[Status] Display buffer: %d lines\n", display_get_buffer_lines());
    Serial.printf("[Status] DRAM free: %u bytes\n", dramFree);
    Serial.printf("[Status] PSRAM: %u/%u used (%u free)\n", psramUsed, psramTotal, psramFree);
    
    if (dramFree < 15000)
        LOG_W(Log::APP, "⚠ DRAM CRITICALLY LOW: %u bytes free", dramFree);
    if (psramFree < 100000)
        LOG_W(Log::APP, "⚠ PSRAM LOW: %u bytes free", psramFree);
    
    Serial.println("================================");
    
    return true;
}

void Manager::returnToLauncher() {
    LOG_I(Log::APP, "returnToLauncher() called from state: %s", stateToStr(s_appState));
    m_pendingReturnToLauncher = true;
}

void Manager::processPendingLaunch() {
    if (m_pendingReturnToLauncher) {
        m_pendingReturnToLauncher = false;
        m_pendingLaunch.clear();
        hideCloseConfirm();
        
        Serial.println("======== RETURNING TO LAUNCHER ========");
#ifndef NO_BLE
        Serial.printf("[Before] BLE: %s, Buffer: %d lines, DRAM: %u\n",
                     BLEBridge::isInitialized() ? "ON" : "OFF",
                     display_get_buffer_lines(),
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#else
        Serial.printf("[Before] BLE: DISABLED, Buffer: %d lines, DRAM: %u\n",
                     display_get_buffer_lines(),
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
#endif
        
        int currentLines = display_get_buffer_lines();
        int bootLines = display_get_boot_lines();
        if (currentLines < bootLines) {
            Serial.printf("[Cleanup] Restoring buffer: %d -> %d lines\n", currentLines, bootLines);
            display_set_buffer((DisplayBuffer)bootLines);
        }
        
        Serial.printf("[After] Buffer: %d lines, DRAM: %u\n",
                     display_get_buffer_lines(),
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        Serial.println("========================================");
        Serial.flush();
        
        showLauncher();
        return;
    }
    
    if (!m_pendingLaunch.empty()) {
        P::String name = m_pendingLaunch;
        m_pendingLaunch.clear();
        launch(name);
    }
}

} // namespace App