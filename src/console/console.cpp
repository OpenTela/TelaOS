/**
 * Console v3 - Command execution
 * 
 * Transport-agnostic. Returns Result, transport handles delivery.
 */

#include "console/console.h"
#include "core/sys_paths.h"
#include "ble/bin_receive.h"
#ifndef NO_BLE
#include "ble/ble_bridge.h"
#endif
#include "core/app_manager.h"
#include "core/state_store.h"
#include "ui/ui_engine.h"
#include "ui/ui_touch.h"
#include "utils/screenshot.h"
#include "utils/log_config.h"
#include "hal/display_hal.h"
#include <lvgl.h>
#include <LittleFS.h>

// OS version constants
static constexpr const char* PROTOCOL_VERSION = "2.7";
static constexpr const char* OS_VERSION = "0.4.0";

static const char* TAG = "Console";

namespace Console {

// === Helpers ===

static const char* argStr(JsonArray args, int idx, const char* def = "") {
    if (idx < (int)args.size() && args[idx].is<const char*>()) {
        return args[idx].as<const char*>();
    }
    return def;
}

// Check if string is a valid integer (optional leading minus, all digits)
static bool isIntStr(const char* s) {
    if (!s || !*s) return false;
    if (*s == '-') s++;
    if (!*s) return false;  // lone "-"
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        s++;
    }
    return true;
}

static int argInt(JsonArray args, int idx, int def = 0) {
    if (idx < (int)args.size()) {
        if (args[idx].is<int>()) return args[idx].as<int>();
        if (args[idx].is<const char*>()) {
            const char* s = args[idx].as<const char*>();
            if (isIntStr(s)) return atoi(s);
        }
    }
    return def;
}

// === UI Subsystem ===

static Result execUi(const char* cmd, JsonArray args) {
    // ui nav <page>
    if (strcmp(cmd, "nav") == 0) {
        const char* page = argStr(args, 0);
        if (!page[0]) return Result::errInvalid("Usage: ui nav <page>");
        
        UI::Engine::instance().showPage(page);
        LOG_I(Log::UI, "ui nav %s", page);
        return Result::ok();
    }
    
    // ui set <widget> <value>
    if (strcmp(cmd, "set") == 0) {
        const char* widget = argStr(args, 0);
        if (!widget[0]) return Result::errInvalid("Usage: ui set <widget> <value>");
        
        // Convert any JSON type to string
        P::String value;
        if (args.size() > 1) {
            JsonVariant v = args[1];
            if (v.is<const char*>()) {
                value = v.as<const char*>();
            } else if (v.is<int>()) {
                value = std::to_string(v.as<int>()).c_str();
            } else if (v.is<float>() || v.is<double>()) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2f", v.as<float>());
                value = buf;
            } else if (v.is<bool>()) {
                value = v.as<bool>() ? "true" : "false";
            }
        }
        
        State::store().setFromString(widget, value);
        LOG_I(Log::UI, "ui set %s = %s", widget, value.c_str());
        return Result::ok();
    }
    
    // ui get <widget>
    if (strcmp(cmd, "get") == 0) {
        const char* widget = argStr(args, 0);
        if (!widget[0]) return Result::errInvalid("Usage: ui get <widget>");
        
        auto r = Result::ok();
        r.data["name"] = widget;
        r.data["value"] = State::store().getString(widget).c_str();
        return r;
    }
    
    // ui text <widget> <value> — alias for set
    if (strcmp(cmd, "text") == 0) {
        const char* widget = argStr(args, 0);
        const char* value = argStr(args, 1);
        if (!widget[0]) return Result::errInvalid("Usage: ui text <widget> <value>");
        
        State::store().set(widget, value);
        return Result::ok();
    }
    
    // ui notify <title> <message>
    if (strcmp(cmd, "notify") == 0) {
        const char* title = argStr(args, 0);
        const char* msg = argStr(args, 1);
        // TODO: implement notification system
        LOG_I(Log::UI, "notify: %s - %s", title, msg);
        return Result::ok();
    }
    
    // ui call <function> [args...]
    if (strcmp(cmd, "call") == 0) {
        const char* func = argStr(args, 0);
        if (!func[0]) return Result::errInvalid("Usage: ui call <function>");
        
        if (!UI::callFunction(func))
            return Result::errInvalid("No script handler registered");
        return Result::ok();
    }
    
    // ui tap <x> <y>  OR  ui tap <widgetId>  (alias: click)
    if (strcmp(cmd, "tap") == 0 || strcmp(cmd, "click") == 0) {
        int x = argInt(args, 0, -1);
        int y = argInt(args, 1, -1);
        
        if (x < 0) {
            // Widget ID mode — try onclick first, fallback to coordinate tap
            const char* wid = argStr(args, 0);
            if (!wid[0]) return Result::errInvalid("Usage: ui tap <widgetId> | <x> <y>");
            if (UI::triggerClick(wid))
                return Result::ok();
            // No onclick — try coordinate-based tap
            if (!UI::getWidgetCenter(wid, x, y))
                return Result::errInvalid("Widget not found");
        }
        
        if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
            return Result::errInvalid("Coordinates out of screen bounds");
        
        TouchSim::tap(x, y);
        return Result::ok();
    }
    
    // ui hold <x> <y> [ms]  OR  ui hold <widgetId> [ms]
    if (strcmp(cmd, "hold") == 0) {
        int x = argInt(args, 0, -1);
        int y, ms;
        
        if (x < 0) {
            // Widget ID mode: hold <widgetId> [ms]
            const char* wid = argStr(args, 0);
            if (!wid[0]) return Result::errInvalid("Usage: ui hold <widgetId> [ms] | <x> <y> [ms]");
            if (!UI::getWidgetCenter(wid, x, y))
                return Result::errInvalid("Widget not found");
            ms = argInt(args, 1, 500);
        } else {
            // Coordinate mode: hold <x> <y> [ms]
            y = argInt(args, 1, -1);
            ms = argInt(args, 2, 500);
        }
        
        if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
            return Result::errInvalid("Coordinates out of screen bounds");
        
        TouchSim::hold(x, y, ms);
        return Result::ok();
    }
    
    // ui swipe <direction> [speed]  OR  ui swipe <x1> <y1> <x2> <y2> [ms]
    if (strcmp(cmd, "swipe") == 0) {
        const char* a0 = argStr(args, 0);
        if (!a0[0]) return Result::errInvalid("Usage: ui swipe <left|right|up|down> [speed]");
        
        // Check if first arg is a direction name
        bool isDirection = false;
        int x1, y1, x2, y2, ms;
        int cx = SCREEN_WIDTH / 2;
        int cy = SCREEN_HEIGHT / 2;
        int lo = SCREEN_WIDTH / 5;      // 20%
        int hi = SCREEN_WIDTH * 4 / 5;  // 80%
        
        if (strcmp(a0, "left") == 0 || strcmp(a0, "l") == 0) {
            x1 = hi; y1 = cy; x2 = lo; y2 = cy; isDirection = true;
        } else if (strcmp(a0, "right") == 0 || strcmp(a0, "r") == 0) {
            x1 = lo; y1 = cy; x2 = hi; y2 = cy; isDirection = true;
        } else if (strcmp(a0, "up") == 0 || strcmp(a0, "u") == 0) {
            x1 = cx; y1 = hi; x2 = cx; y2 = lo; isDirection = true;
        } else if (strcmp(a0, "down") == 0 || strcmp(a0, "d") == 0) {
            x1 = cx; y1 = lo; x2 = cx; y2 = hi; isDirection = true;
        }
        
        if (isDirection) {
            // Parse speed: fast=150, normal=300, slow=500
            const char* speed = argStr(args, 1);
            ms = 300;  // default normal
            if (strcmp(speed, "fast") == 0) ms = 150;
            else if (strcmp(speed, "slow") == 0) ms = 500;
        } else {
            // Coordinate mode: swipe x1 y1 x2 y2 [ms]
            x1 = argInt(args, 0, -1);
            y1 = argInt(args, 1, -1);
            x2 = argInt(args, 2, -1);
            y2 = argInt(args, 3, -1);
            ms = argInt(args, 4, 300);
            if (x1 < 0 || y1 < 0 || x2 < 0 || y2 < 0)
                return Result::errInvalid("Usage: ui swipe <x1> <y1> <x2> <y2> [ms]");
        }
        
        TouchSim::swipe(x1, y1, x2, y2, ms);
        return Result::ok();
    }
    
    // ui type <text>  OR  ui type <widgetId> <text>
    if (strcmp(cmd, "type") == 0) {
        const char* text;
        
        if (args.size() >= 2) {
            // type <widgetId> <text> — focus widget first, then type
            const char* wid = argStr(args, 0);
            text = argStr(args, 1);
            if (!UI::focusInput(wid))
                return Result::errInvalid("Widget not found or not an input");
        } else {
            // type <text> — type into current focus
            text = argStr(args, 0);
            if (!text[0]) return Result::errInvalid("Usage: ui type [widgetId] <text>");
        }
        
        lv_obj_t* focused = UI::getFocusedTextarea();
        if (!focused) {
            lv_group_t* grp = lv_group_get_default();
            if (grp) focused = lv_group_get_focused(grp);
        }
        if (!focused || !lv_obj_check_type(focused, &lv_textarea_class)) {
            return Result::errInvalid("No input widget in focus");
        }
        
        lv_textarea_set_text(focused, text);
        lv_obj_send_event(focused, LV_EVENT_VALUE_CHANGED, NULL);
        LOG_D(Log::UI, "ui type: %s", text);
        return Result::ok();
    }
    
    return Result::errInvalid("Unknown ui command");
}

// === System Subsystem ===

static Result execSys(const char* cmd, JsonArray args) {
    // sys ping
    if (strcmp(cmd, "ping") == 0) {
        return Result::ok();
    }
    
    // sys info
    if (strcmp(cmd, "info") == 0) {
        auto r = Result::ok();
        r.data["heap"] = ESP.getFreeHeap();
        r.data["psram"] = ESP.getFreePsram();
        r.data["chip"] = ESP.getChipModel();
        r.data["freq"] = ESP.getCpuFreqMHz();
        r.data["buf_lines"] = display_get_buffer_lines();
#ifndef NO_BLE
        r.data["ble"] = BLEBridge::isInitialized() ? "on" : "off";
#else
        r.data["ble"] = "disabled";
#endif
        return r;
    }
    
    // sys reboot
    if (strcmp(cmd, "reboot") == 0) {
        LOG_W(Log::APP, "Reboot requested");
        ESP.restart();
        return Result::ok("Rebooting...");
    }
    
    // sys screen [color] [scale] — returns binary screenshot
    if (strcmp(cmd, "screen") == 0) {
        const char* color = argStr(args, 0, "rgb16");
        int scale = argInt(args, 1, 0);
        const char* mode = argStr(args, 2, "fixed");
        
        Screenshot::CaptureResult cap;
        if (!Screenshot::capture(cap, mode, scale, color)) {
            return Result::errServer("Screenshot capture failed");
        }
        
        auto r = Result::ok();
        r.data["w"] = cap.width;
        r.data["h"] = cap.height;
        r.data["color"] = cap.color;
        r.data["format"] = "lz4";
        r.data["raw_size"] = cap.rawSize;
        r.withBinaryData(cap.buffer, cap.size);
        
        LOG_I(Log::APP, "sys screen %s scale=%d -> %ux%u %u bytes", 
              color, scale, cap.width, cap.height, cap.size);
        return r;
    }
    
    // sys time [timestamp]
    if (strcmp(cmd, "time") == 0) {
        if (args.size() > 0) {
            // Set time
            time_t ts = argInt(args, 0, 0);
            struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            LOG_I(Log::APP, "Time set to %ld", ts);
        }
        
        auto r = Result::ok();
        r.data["time"] = (long)time(nullptr);
        return r;
    }
    
    // sys sync <protocol_version> <datetime_iso> <timezone>
    if (strcmp(cmd, "sync") == 0) {
        const char* clientProto = argStr(args, 0);
        const char* datetime = argStr(args, 1);
        const char* tz = argStr(args, 2);
        
        // Parse ISO 8601 datetime → time_t (UTC)
        if (datetime[0]) {
            struct tm tm = {};
            int parsed = sscanf(datetime, "%d-%d-%dT%d:%d:%d",
                &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
            
            if (parsed >= 6) {
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                
                // Force UTC so mktime doesn't apply old timezone
                setenv("TZ", "UTC0", 1);
                tzset();
                
                time_t ts = mktime(&tm);
                struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
                settimeofday(&tv, nullptr);
                LOG_I(Log::APP, "Sync: time set %s (ts=%ld)", datetime, ts);
            }
        }
        
        // Apply timezone (after time is set as UTC)
        if (tz[0]) {
            int tzH = 0, tzM = 0;
            char sign = '+';
            sscanf(tz, "%c%d:%d", &sign, &tzH, &tzM);
            
            // POSIX TZ inverts sign: UTC+3 → "UTC-3"
            int posixH = (sign == '-') ? tzH : -tzH;
            char tzStr[32];
            snprintf(tzStr, sizeof(tzStr), "UTC%+d:%02d", posixH, tzM);
            setenv("TZ", tzStr, 1);
            tzset();
            
            LOG_I(Log::APP, "Sync: timezone %s (POSIX: %s)", tz, tzStr);
        }
        
        auto r = Result::ok();
        r.data["protocol"] = PROTOCOL_VERSION;
        r.data["os"] = OS_VERSION;
        return r;
    }
    
    return Result::errInvalid("Unknown sys command");
}

// === App Subsystem ===

static Result execApp(const char* cmd, JsonArray args) {
    // app list — returns binary: null-terminated app names
    if (strcmp(cmd, "list") == 0) {
        auto& apps = App::Manager::instance().apps();
        
        // Calculate buffer size
        uint32_t size = 0;
        for (const auto& app : apps) {
            size += app.name.length() + 1;  // +1 for null terminator
        }
        
        // Build buffer
        uint8_t* buf = (uint8_t*)ps_malloc(size);
        if (!buf) return Result::errMemory("Out of memory");
        
        uint8_t* p = buf;
        for (const auto& app : apps) {
            memcpy(p, app.name.c_str(), app.name.length() + 1);
            p += app.name.length() + 1;
        }
        
        auto r = Result::ok();
        r.data["count"] = apps.size();
        r.withBinary(buf, size);
        return r;
    }
    
    // app pull <n> [*]
    if (strcmp(cmd, "pull") == 0) {
        const char* name = argStr(args, 0);
        if (!name[0]) return Result::errInvalid("Usage: app pull <n> [*]");
        
        const char* fileArg = argStr(args, 1, "");
        bool allFiles = (strcmp(fileArg, "*") == 0);
        
        P::String basePath = P::String(SYS_APPS) + name;
        
        if (!LittleFS.exists(basePath.c_str())) {
            return Result::errNotFound("App not found");
        }
        
        if (allFiles) {
            // Collect all files and sizes
            struct FileEntry { P::String name; uint32_t size; };
            P::Array<FileEntry> files;
            uint32_t totalSize = 0;
            
            auto scanDir = [&](const char* dirPath, const char* prefix) {
                File dir = LittleFS.open(dirPath);
                if (!dir || !dir.isDirectory()) return;
                File f = dir.openNextFile();
                while (f) {
                    if (!f.isDirectory()) {
                        P::String fname = prefix[0] ? (P::String(prefix) + f.name()) : P::String(f.name());
                        files.push_back({fname, (uint32_t)f.size()});
                        totalSize += f.size();
                    }
                    f = dir.openNextFile();
                }
            };
            
            // Scan root and resources/
            scanDir(basePath.c_str(), "");
            P::String resPath = basePath + "/resources";
            if (LittleFS.exists(resPath.c_str())) {
                scanDir(resPath.c_str(), "resources/");
            }
            
            if (files.empty()) return Result::errNotFound("No files");
            
            // Allocate and concat
            uint8_t* buf = (uint8_t*)ps_malloc(totalSize);
            if (!buf) return Result::errMemory("Out of memory");
            
            auto r = Result::ok();
            r.data["name"] = name;
            JsonObject filesObj = r.data["files"].to<JsonObject>();
            
            uint32_t offset = 0;
            for (const auto& entry : files) {
                P::String fullPath = basePath + "/" + entry.name;
                File f = LittleFS.open(fullPath.c_str(), "r");
                if (f) {
                    f.read(buf + offset, entry.size);
                    f.close();
                    filesObj[entry.name] = entry.size;
                    offset += entry.size;
                }
            }
            
            r.withBinary(buf, totalSize);
            LOG_I(Log::APP, "app pull %s * (%u files, %u bytes)", name, (unsigned)files.size(), totalSize);
            return r;
        }
        
        // Single file: {name}/{name}.bax
        P::String path = basePath + "/" + P::String(name) + ".bax";
        if (!LittleFS.exists(path.c_str())) return Result::errNotFound("App not found");
        
        File f = LittleFS.open(path.c_str(), "r");
        if (!f) return Result::errNotFound("App not found");
        
        uint32_t size = f.size();
        uint8_t* buf = (uint8_t*)ps_malloc(size);
        if (!buf) {
            f.close();
            return Result::errMemory("Out of memory");
        }
        
        f.read(buf, size);
        f.close();
        
        auto r = Result::ok();
        r.data["name"] = name;
        r.data["size"] = size;
        r.withBinary(buf, size);
        LOG_I(Log::APP, "app pull %s (%u bytes)", name, size);
        return r;
    }
    
    // app push <name> [file] <size>    — single file (file defaults to {name}.bax)
    // app push <name> * <files_json>   — multi-file
    if (strcmp(cmd, "push") == 0) {
        const char* name = argStr(args, 0);
        const char* sizeOrStar = argStr(args, 1, "");
        
        if (!name[0] || !sizeOrStar[0]) {
            return Result::errInvalid("Usage: app push <name> [file] <size>");
        }
        
        // Имя файла должно содержать точку (расширение)
        auto hasExtension = [](const char* s) -> bool {
            if (!s || !s[0]) return false;
            return strchr(s, '.') != nullptr;
        };
        
        // Multi-file: app push myapp * {"weather.bax":1984,"icon.png":512}
        if (strcmp(sizeOrStar, "*") == 0) {
            const char* filesJson = argStr(args, 2, "");
            if (!filesJson[0]) {
                return Result::errInvalid("Usage: app push <n> * <files_json>");
            }
            
            // Parse files JSON
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, filesJson);
            if (err) {
                return Result::errInvalid("Invalid files JSON");
            }
            
            BinReceive::FileEntry entries[16];
            uint32_t count = 0;
            uint32_t totalSize = 0;
            
            JsonObject obj = doc.as<JsonObject>();
            for (auto kv : obj) {
                if (count >= 16) break;
                strncpy(entries[count].name, kv.key().c_str(), sizeof(entries[count].name) - 1);
                entries[count].name[sizeof(entries[count].name) - 1] = 0;
                entries[count].size = kv.value().as<uint32_t>();
                totalSize += entries[count].size;
                count++;
            }
            
            if (count == 0 || totalSize == 0) {
                return Result::errInvalid("Empty files list");
            }
            
            // Validate all filenames have extensions
            for (uint32_t i = 0; i < count; i++) {
                if (!hasExtension(entries[i].name)) {
                    return Result::errInvalid("Filename must have extension (e.g. calc.bax)");
                }
            }
            
            if (!BinReceive::startMulti(name, entries, count, totalSize)) {
                return Result::errMemory("Failed to start receive");
            }
            
            auto r = Result::ok("Ready to receive");
            r.data["name"] = name;
            JsonObject filesObj = r.data["files"].to<JsonObject>();
            for (uint32_t i = 0; i < count; i++) {
                filesObj[entries[i].name] = entries[i].size;
            }
            r.data["size"] = totalSize;
            
            LOG_I(Log::APP, "app push %s * (%u files, %u bytes)", name, count, totalSize);
            return r;
        }
        
        // Single file: app push myapp 1984
        //              app push myapp calc.bax 1984
        // Размер = только цифры, без точки. Всё остальное = имя файла.
        auto isSize = [](const char* s) -> bool {
            if (!s || !s[0]) return false;
            for (const char* p = s; *p; p++) {
                if (*p < '0' || *p > '9') return false;
            }
            return true;
        };
        
        const char* arg2 = argStr(args, 2, "");
        int size = 0;
        char defaultFile[48];
        snprintf(defaultFile, sizeof(defaultFile), "%s.bax", name);
        const char* fileName = defaultFile;
        
        if (isSize(sizeOrStar)) {
            size = atoi(sizeOrStar);
            if (arg2[0]) fileName = arg2;
        } else if (isSize(arg2)) {
            fileName = sizeOrStar;
            size = atoi(arg2);
        } else {
            size = atoi(sizeOrStar); // fallback
        }
        
        if (size <= 0) {
            return Result::errInvalid("Usage: app push <n> [file] <size>");
        }
        
        if (!hasExtension(fileName)) {
            return Result::errInvalid("Filename must have extension (e.g. calc.bax)");
        }
        
        if (!BinReceive::start(name, fileName, size)) {
            return Result::errMemory("Failed to start receive");
        }
        
        auto r = Result::ok("Ready to receive");
        r.data["name"] = name;
        r.data["file"] = fileName;
        r.data["size"] = size;
        
        LOG_I(Log::APP, "app push %s/%s (%d bytes) - waiting for data", name, fileName, size);
        return r;
    }
    // app delete <name>
    if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "rm") == 0) {
        const char* name = argStr(args, 0);
        if (!name[0]) return Result::errInvalid("Usage: app delete <name>");
        
        P::String path = P::String(SYS_APPS) + name;
        
        // Delete all files in app directory
        File dir = LittleFS.open(path.c_str());
        if (dir && dir.isDirectory()) {
            File f;
            while ((f = dir.openNextFile())) {
                LittleFS.remove(f.path());
            }
        }
        LittleFS.rmdir(path.c_str());
        
        LOG_I(Log::APP, "app delete %s", name);
        return Result::ok();
    }
    
    // app launch <name>
    if (strcmp(cmd, "launch") == 0 || strcmp(cmd, "run") == 0) {
        const char* name = argStr(args, 0);
        if (!name[0]) return Result::errInvalid("Usage: app launch <name>");
        
        App::Manager::instance().queueLaunch(name); if (true) {
            return Result::ok();
        }
        return Result::err(ErrorCode::SERVER, "Failed to launch app");
    }
    
    // app home
    if (strcmp(cmd, "home") == 0) {
        App::Manager::instance().returnToLauncher();
        return Result::ok();
    }
    
    return Result::errInvalid("Unknown app command");
}

// === Log Subsystem ===

static Result execLog(const char* cmd, JsonArray args) {
    // log [category] [level]
    // log                  — show all levels
    // log verbose          — set all to verbose
    // log ui debug         — set UI to debug
    
    if (!cmd[0] || strcmp(cmd, "show") == 0) {
        // Show current levels
        auto r = Result::ok();
        r.data["UI"] = Log::levelName(Log::get(Log::UI));
        r.data["LUA"] = Log::levelName(Log::get(Log::LUA));
        r.data["STATE"] = Log::levelName(Log::get(Log::STATE));
        r.data["APP"] = Log::levelName(Log::get(Log::APP));
        r.data["BLE"] = Log::levelName(Log::get(Log::BLE));
        return r;
    }
    
    // Parse level
    Log::Level level = Log::Info;
    const char* levelStr = args.size() > 0 ? argStr(args, 0) : cmd;
    
    if (strcmp(levelStr, "verbose") == 0 || strcmp(levelStr, "v") == 0) level = Log::Verbose;
    else if (strcmp(levelStr, "debug") == 0 || strcmp(levelStr, "d") == 0) level = Log::Debug;
    else if (strcmp(levelStr, "info") == 0 || strcmp(levelStr, "i") == 0) level = Log::Info;
    else if (strcmp(levelStr, "warn") == 0 || strcmp(levelStr, "w") == 0) level = Log::Warn;
    else if (strcmp(levelStr, "error") == 0 || strcmp(levelStr, "e") == 0) level = Log::Error;
    else if (strcmp(levelStr, "disabled") == 0 || strcmp(levelStr, "off") == 0) level = Log::Disabled;
    else {
        // First arg is category
        Log::Cat cat = Log::APP;
        if (strcmp(cmd, "ui") == 0) cat = Log::UI;
        else if (strcmp(cmd, "lua") == 0) cat = Log::LUA;
        else if (strcmp(cmd, "state") == 0) cat = Log::STATE;
        else if (strcmp(cmd, "app") == 0) cat = Log::APP;
        else if (strcmp(cmd, "ble") == 0) cat = Log::BLE;
        else return Result::errInvalid("Unknown category");
        
        // Second arg is level
        levelStr = argStr(args, 0, "info");
        if (strcmp(levelStr, "verbose") == 0) level = Log::Verbose;
        else if (strcmp(levelStr, "debug") == 0) level = Log::Debug;
        else if (strcmp(levelStr, "info") == 0) level = Log::Info;
        else if (strcmp(levelStr, "warn") == 0) level = Log::Warn;
        else if (strcmp(levelStr, "error") == 0) level = Log::Error;
        else if (strcmp(levelStr, "disabled") == 0) level = Log::Disabled;
        
        Log::set(cat, level);
        
        auto r = Result::ok();
        r.data["category"] = cmd;
        r.data["level"] = Log::levelName(level);
        return r;
    }
    
    // Set all categories
    Log::set(Log::UI, level);
    Log::set(Log::LUA, level);
    Log::set(Log::STATE, level);
    Log::set(Log::APP, level);
    Log::set(Log::BLE, level);
    
    auto r = Result::ok();
    r.data["all"] = Log::levelName(level);
    return r;
}

// === Main Entry Points ===

Result exec(const char* subsystem, const char* cmd, JsonArray args) {
    if (strcmp(subsystem, "ui") == 0) return execUi(cmd, args);
    if (strcmp(subsystem, "sys") == 0) return execSys(cmd, args);
    if (strcmp(subsystem, "app") == 0) return execApp(cmd, args);
    if (strcmp(subsystem, "log") == 0) return execLog(cmd, args);
    
    return Result::errInvalid("Unknown subsystem");
}

Result exec(const char* line) {
    // Parse: "subsystem cmd arg1 arg2 ..."
    // Supports quoted args: ui type editInput "Hello World"
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    
    // Tokenize with quote support
    char* tokens[16];
    int ntokens = 0;
    char* p = buf;
    
    while (*p && ntokens < 16) {
        // Skip whitespace
        while (*p == ' ') p++;
        if (!*p) break;
        
        if (*p == '"') {
            // Quoted token: find closing quote
            p++;  // skip opening "
            tokens[ntokens++] = p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = '\0';  // terminate & skip closing "
        } else {
            // Unquoted token
            tokens[ntokens++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = '\0';
        }
    }
    
    if (ntokens == 0) return Result::errInvalid("Empty command");
    
    char* subsystem = tokens[0];
    char* cmd;
    int argStart;
    
    if (ntokens >= 2) {
        cmd = tokens[1];
        argStart = 2;
    } else {
        cmd = subsystem;
        subsystem = (char*)"sys";
        argStart = 1;
    }
    
    // Collect remaining args
    JsonDocument doc;
    JsonArray args = doc.to<JsonArray>();
    
    for (int i = argStart; i < ntokens; i++) {
        args.add(tokens[i]);
    }
    
    return exec(subsystem, cmd, args);
}

} // namespace Console
