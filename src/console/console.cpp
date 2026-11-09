/**
 * Console v3 - Command execution
 * 
 * Transport-agnostic. Returns Result, transport handles delivery.
 */

#include "console/console.h"
#include "core/sys_paths.h"
#include "ble/bin_receive.h"
#include "ota/ota_receive.h"
#ifndef NO_BLE
#include "ble/ble_bridge.h"
#endif
#include "core/app_manager.h"
#include "core/state_store.h"
#include "core/core.h"
#include "ui/ui_touch.h"
#include "utils/screenshot.h"
#include "utils/log_config.h"
#include "hal/display_hal.h"
#include <lvgl.h>
#include <LittleFS.h>
#include <algorithm>
#include <ctime>
#include <string>
#include <string_view>

// OS version constants
static constexpr const char* PROTOCOL_VERSION = "2.7";
static constexpr const char* OS_VERSION = "0.4.0";

static const char* TAG = "Console";

// Default: 100%. Override in hardware-specific code.
__attribute__((weak)) int getBatteryPercent() { return 100; }

namespace Console {

// === Helpers ===

static const char* argStr(JsonArray args, int idx, const char* def = "") {
    if (idx < (int)args.size() && args[idx].is<const char*>()) {
        return args[idx].as<const char*>();
    }
    return def;
}

// Digits only, at least one
static bool isDigits(std::string_view s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Check if string is a valid integer (optional leading minus, all digits)
static bool isIntStr(std::string_view s) {
    if (!s.empty() && s.front() == '-') s.remove_prefix(1);
    return isDigits(s);  // a lone "-" is not
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

static Result execUi(std::string_view cmd, JsonArray args) {
    // ui nav <page>
    if (cmd == "nav") {
        const char* page = argStr(args, 0);
        if (!page[0]) return Result::errInvalid("Usage: ui nav <page>");
        
        g_core.showPage(page);
        LOG_I(Log::UI, "ui nav %s", page);
        return Result::ok();
    }
    
    // ui set <widget> <value>
    if (cmd == "set") {
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
                value = Store::floatToString(v.as<float>());
            } else if (v.is<bool>()) {
                value = v.as<bool>() ? "true" : "false";
            }
        }
        
        g_core.store().setFromString(widget, value);
        LOG_I(Log::UI, "ui set %s = %s", widget, value.c_str());
        return Result::ok();
    }
    
    // ui get <widget>
    if (cmd == "get") {
        const char* widget = argStr(args, 0);
        if (!widget[0]) return Result::errInvalid("Usage: ui get <widget>");
        
        auto r = Result::ok();
        r.data["name"] = widget;
        r.data["value"] = g_core.store().getString(widget).c_str();
        return r;
    }
    
    // ui text <widget> <value> — alias for set
    if (cmd == "text") {
        const char* widget = argStr(args, 0);
        const char* value = argStr(args, 1);
        if (!widget[0]) return Result::errInvalid("Usage: ui text <widget> <value>");
        
        g_core.store().set(widget, value);
        return Result::ok();
    }
    
    // ui notify <title> <message>
    if (cmd == "notify") {
        const char* title = argStr(args, 0);
        const char* msg = argStr(args, 1);
        // TODO: implement notification system
        LOG_I(Log::UI, "notify: %s - %s", title, msg);
        return Result::ok();
    }
    
    // ui call <function> [args...]
    if (cmd == "call") {
        const char* func = argStr(args, 0);
        if (!func[0]) return Result::errInvalid("Usage: ui call <function>");
        
        if (!callFunction(func))
            return Result::errInvalid("No script handler registered");
        return Result::ok();
    }
    
    // ui tap <x> <y>  OR  ui tap <widgetId>  (alias: click)
    if (cmd == "tap" || cmd == "click") {
        int x = argInt(args, 0, -1);
        int y = argInt(args, 1, -1);
        
        if (x < 0) {
            // Widget ID mode — try onclick first, fallback to coordinate tap
            const char* wid = argStr(args, 0);
            if (!wid[0]) return Result::errInvalid("Usage: ui tap <widgetId> | <x> <y>");
            if (triggerClick(wid))
                return Result::ok();
            // No onclick — try coordinate-based tap
            if (!getWidgetCenter(wid, x, y))
                return Result::errInvalid("Widget not found");
        }
        
        if (x < 0 || y < 0 || x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
            return Result::errInvalid("Coordinates out of screen bounds");
        
        TouchSim::tap(x, y);
        return Result::ok();
    }
    
    // ui hold <x> <y> [ms]  OR  ui hold <widgetId> [ms]
    if (cmd == "hold") {
        int x = argInt(args, 0, -1);
        int y, ms;
        
        if (x < 0) {
            // Widget ID mode: hold <widgetId> [ms]
            const char* wid = argStr(args, 0);
            if (!wid[0]) return Result::errInvalid("Usage: ui hold <widgetId> [ms] | <x> <y> [ms]");
            if (!getWidgetCenter(wid, x, y))
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
    if (cmd == "swipe") {
        const std::string_view a0 = argStr(args, 0);
        if (a0.empty()) return Result::errInvalid("Usage: ui swipe <left|right|up|down> [speed]");
        
        // Check if first arg is a direction name
        bool isDirection = false;
        int x1, y1, x2, y2, ms;
        int cx = SCREEN_WIDTH / 2;
        int cy = SCREEN_HEIGHT / 2;
        int lo = SCREEN_WIDTH / 5;      // 20%
        int hi = SCREEN_WIDTH * 4 / 5;  // 80%
        
        if (a0 == "left" || a0 == "l") {
            x1 = hi; y1 = cy; x2 = lo; y2 = cy; isDirection = true;
        } else if (a0 == "right" || a0 == "r") {
            x1 = lo; y1 = cy; x2 = hi; y2 = cy; isDirection = true;
        } else if (a0 == "up" || a0 == "u") {
            x1 = cx; y1 = hi; x2 = cx; y2 = lo; isDirection = true;
        } else if (a0 == "down" || a0 == "d") {
            x1 = cx; y1 = lo; x2 = cx; y2 = hi; isDirection = true;
        }
        
        if (isDirection) {
            // Parse speed: fast=150, normal=300, slow=500
            const std::string_view speed = argStr(args, 1);
            ms = 300;  // default normal
            if (speed == "fast") ms = 150;
            else if (speed == "slow") ms = 500;
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
    if (cmd == "type") {
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
        
        lv_obj_t* focused = getFocusedTextarea();
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

static Result execSys(std::string_view cmd, JsonArray args) {
    // sys ping
    if (cmd == "ping") {
        return Result::ok();
    }

    // sys ota <raw_size> <comp_size> [sha256_hex]
    //   Arms a full-firmware OTA receive over the BIN channel.
    //   comp_size == 0 -> uncompressed; > 0 -> one LZ4 block of raw_size bytes.
    //   Client then streams the (compressed) image as [2B chunk_id][data] chunks.
    if (cmd == "ota") {
        const char* rawStr  = argStr(args, 0, "");
        const char* compStr = argStr(args, 1, "0");
        const char* sha     = argStr(args, 2, "");

        long raw  = atol(rawStr);
        long comp = atol(compStr);
        if (raw <= 0 || comp < 0) {
            return Result::errInvalid("Usage: sys ota <raw_size> <comp_size> [sha256]");
        }

        if (!OtaReceive::start((uint32_t)raw, (uint32_t)comp, sha)) {
            return Result::errMemory("Failed to start OTA (bad size or no PSRAM)");
        }

        auto r = Result::ok("Ready for OTA");
        r.data["raw"]  = (uint32_t)raw;
        r.data["comp"] = (uint32_t)comp;
        r.data["hash"] = (sha && sha[0]) ? sha : "none";
        return r;
    }
    
    // sys info
    if (cmd == "info") {
        auto r = Result::ok();
        r.data["heap_free"] = ESP.getFreeHeap();
        r.data["heap_min"] = ESP.getMinFreeHeap();
        r.data["psram_free"] = ESP.getFreePsram();
        r.data["psram_total"] = ESP.getPsramSize();
        r.data["chip"] = ESP.getChipModel();
        r.data["freq"] = ESP.getCpuFreqMHz();
        r.data["buf_lines"] = display_get_buffer_lines();
        
        auto& mgr = App::Manager::instance();
        if (!mgr.inLauncher() && mgr.currentApp().length() > 0) {
            // Extract name from path: "/apps/calculator/calculator.bax" → "calculator"
            const auto& path = mgr.currentApp();
            size_t lastSlash = path.rfind('/');
            size_t prevSlash = (lastSlash != std::string::npos && lastSlash > 0) 
                               ? path.rfind('/', lastSlash - 1) : std::string::npos;
            if (prevSlash != std::string::npos) {
                r.data["app"] = path.substr(prevSlash + 1, lastSlash - prevSlash - 1).c_str();
            } else {
                r.data["app"] = path.c_str();
            }
        } else {
            r.data["app"] = (const char*)nullptr;
        }
        
#ifndef NO_BLE
        r.data["ble"] = BLEBridge::isInitialized() ? "on" : "off";
#else
        r.data["ble"] = "disabled";
#endif
        return r;
    }
    
    // sys reboot
    if (cmd == "reboot") {
        LOG_W(Log::APP, "Reboot requested");
        ESP.restart();
        return Result::ok("Rebooting...");
    }
    
    // sys screen [color] [scale] — returns binary screenshot
    if (cmd == "screen") {
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
    if (cmd == "time") {
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
    // sys sync <protocol> <datetime> <timezone> [lang]
    if (cmd == "sync") {
        const char* clientProto = argStr(args, 0);
        const char* datetime = argStr(args, 1);
        const char* tz = argStr(args, 2);
        const char* lang = argStr(args, 3, "");
        
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
            
            // Handle formats: "+3", "-5", "+3:00", "5", "-5:30"
            if (tz[0] == '+' || tz[0] == '-') {
                sign = tz[0];
                sscanf(tz + 1, "%d:%d", &tzH, &tzM);
            } else {
                sscanf(tz, "%d:%d", &tzH, &tzM);
            }
            
            // POSIX TZ inverts sign: UTC+3 → "UTC-3". Hours always signed, minutes in two digits
            int posixH = (sign == '-') ? tzH : -tzH;
            P::String tzStr = "UTC";
            if (posixH >= 0) tzStr += '+';
            tzStr += std::to_string(posixH).c_str();
            tzStr += ':';
            if (tzM >= 0 && tzM < 10) tzStr += '0';
            tzStr += std::to_string(tzM).c_str();
            setenv("TZ", tzStr.c_str(), 1);
            tzset();
            
            LOG_I(Log::APP, "Sync: timezone %s (POSIX: %s)", tz, tzStr.c_str());
        }
        
        // Store lang if provided
        if (lang[0]) {
            g_core.store().set("sys.lang", lang);
            LOG_I(Log::APP, "Sync: lang=%s", lang);
        }
        
        auto r = Result::ok();
        r.data["protocol"] = PROTOCOL_VERSION;
        r.data["os"] = OS_VERSION;
        r.data["chip"] = ESP.getChipModel();
        r.data["time"] = (long)time(nullptr);
        r.data["uptime"] = (long)(millis() / 1000);
        r.data["battery"] = getBatteryPercent();
        r.data["w"] = SCREEN_WIDTH;
        r.data["h"] = SCREEN_HEIGHT;
        return r;
    }
    
    return Result::errInvalid("Unknown sys command");
}

// === App Subsystem ===

static Result execApp(std::string_view cmd, JsonArray args) {
    // app list — returns binary: null-terminated app names
    if (cmd == "list") {
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
    
    // app info <n> — list files and total size
    if (cmd == "info") {
        const char* name = argStr(args, 0);
        if (!name[0]) return Result::errInvalid("Usage: app info <n>");
        
        P::String basePath = P::String(SYS_APPS) + name;
        if (!LittleFS.exists(basePath.c_str())) {
            return Result::errNotFound("App not found");
        }
        
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
        
        scanDir(basePath.c_str(), "");
        P::String resPath = basePath + "/resources";
        if (LittleFS.exists(resPath.c_str())) {
            scanDir(resPath.c_str(), "resources/");
        }
        
        auto r = Result::ok();
        r.data["name"] = name;
        r.data["size"] = totalSize;
        JsonObject filesObj = r.data["files"].to<JsonObject>();
        for (const auto& entry : files) {
            filesObj[entry.name] = entry.size;
        }
        
        LOG_I(Log::APP, "app info %s: %u files, %u bytes", name, (unsigned)files.size(), totalSize);
        return r;
    }
    
    // app pull <n> [file|*]
    if (cmd == "pull") {
        const char* name = argStr(args, 0);
        if (!name[0]) return Result::errInvalid("Usage: app pull <n> [file|*]");
        
        const char* fileArg = argStr(args, 1, "");
        bool allFiles = std::string_view(fileArg) == "*";
        
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
        
        // Single file: default {name}.bax, or specific file if provided
        P::String fileName = fileArg[0] ? P::String(fileArg) : (P::String(name) + ".bax");
        P::String path = basePath + "/" + fileName;
        if (!LittleFS.exists(path.c_str())) return Result::errNotFound("File not found");
        
        File f = LittleFS.open(path.c_str(), "r");
        if (!f) return Result::errNotFound("File not found");
        
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
        r.data["file"] = fileName;
        r.data["size"] = size;
        r.withBinary(buf, size);
        LOG_I(Log::APP, "app pull %s/%s (%u bytes)", name, fileName.c_str(), size);
        return r;
    }
    
    // app push <name> [file] <size>    — single file (file defaults to {name}.bax)
    // app push <name> * <files_json>   — multi-file
    if (cmd == "push") {
        const char* name = argStr(args, 0);
        const char* sizeOrStar = argStr(args, 1, "");
        
        if (!name[0] || !sizeOrStar[0]) {
            return Result::errInvalid("Usage: app push <name> [file] <size>");
        }
        
        // Имя файла должно содержать точку (расширение)
        auto hasExtension = [](std::string_view s) -> bool {
            return s.find('.') != std::string_view::npos;
        };
        
        // Multi-file: app push myapp * {"weather.bax":1984,"icon.png":512}
        if (std::string_view(sizeOrStar) == "*") {
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
                // FileEntry is the receiver's (bin_receive.h): a name is a char[48] there
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
        const char* arg2 = argStr(args, 2, "");
        int size = 0;
        // <name>.bax, cut to 47 characters as before: the receiver keeps no more (FileEntry::name)
        P::String fileName = P::String(name) + ".bax";
        fileName.resize(std::min(fileName.size(), sizeof(BinReceive::FileEntry::name) - 1));
        
        if (isDigits(sizeOrStar)) {
            size = atoi(sizeOrStar);
            if (arg2[0]) fileName = arg2;
        } else if (isDigits(arg2)) {
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
        
        if (!BinReceive::start(name, fileName.c_str(), size)) {
            return Result::errMemory("Failed to start receive");
        }
        
        auto r = Result::ok("Ready to receive");
        r.data["name"] = name;
        r.data["file"] = fileName;
        r.data["size"] = size;
        
        LOG_I(Log::APP, "app push %s/%s (%d bytes) - waiting for data", name, fileName.c_str(), size);
        return r;
    }
    // app delete <name>
    if (cmd == "delete" || cmd == "rm") {
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
    if (cmd == "launch" || cmd == "run") {
        const char* name = argStr(args, 0);
        if (!name[0]) return Result::errInvalid("Usage: app launch <name>");
        
        App::Manager::instance().queueLaunch(name); if (true) {
            return Result::ok();
        }
        return Result::err(ErrorCode::SERVER, "Failed to launch app");
    }
    
    // app home
    if (cmd == "home") {
        App::Manager::instance().returnToLauncher();
        return Result::ok();
    }
    
    return Result::errInvalid("Unknown app command");
}

// === Log Subsystem ===

// The categories `log` shows and sets, in this order; key: the name `log show` gives it
struct LogCategory {
    const char* name;
    const char* key;
    Log::Cat cat;
};
static constexpr LogCategory kLogCategories[] = {
    {"ui", "UI", Log::UI}, {"lua", "LUA", Log::LUA}, {"state", "STATE", Log::STATE},
    {"app", "APP", Log::APP}, {"ble", "BLE", Log::BLE},
};

struct LogLevelName {
    const char* name;
    const char* shortName;
    Log::Level level;
};
static constexpr LogLevelName kLogLevels[] = {
    {"verbose", "v", Log::Verbose}, {"debug", "d", Log::Debug}, {"info", "i", Log::Info},
    {"warn", "w", Log::Warn}, {"error", "e", Log::Error}, {"disabled", "off", Log::Disabled},
};

// The level named `s` into `level`; shortNames: v, d, i, w, e and off count too
static bool levelNamed(std::string_view s, bool shortNames, Log::Level& level) {
    for (const auto& l : kLogLevels) {
        if (s == l.name || (shortNames && s == l.shortName)) {
            level = l.level;
            return true;
        }
    }
    return false;
}

static Result execLog(std::string_view cmd, JsonArray args) {
    // log [category] [level]
    // log                  — show all levels
    // log verbose          — set all to verbose
    // log ui debug         — set UI to debug
    
    if (cmd.empty() || cmd == "show") {
        // Show current levels
        auto r = Result::ok();
        for (const auto& c : kLogCategories) r.data[c.key] = Log::levelName(Log::get(c.cat));
        return r;
    }
    
    // log <category> [level]: that category only; no level is info
    for (const auto& c : kLogCategories) {
        if (cmd != c.name) continue;
        Log::Level level = Log::Info;
        if (args.size() > 0 && !levelNamed(argStr(args, 0), true, level)) {
            return Result::errInvalid("Unknown level");
        }
        Log::set(c.cat, level);

        auto r = Result::ok();
        r.data["category"] = c.name;
        r.data["level"] = Log::levelName(level);
        return r;
    }

    // log <level>: every category
    Log::Level level = Log::Info;
    if (!levelNamed(cmd, true, level)) return Result::errInvalid("Unknown category");
    for (const auto& c : kLogCategories) Log::set(c.cat, level);
    
    auto r = Result::ok();
    r.data["all"] = Log::levelName(level);
    return r;
}

// === Main Entry Points ===

Result exec(const char* subsystem, const char* cmd, JsonArray args) {
    const std::string_view sub = subsystem;
    if (sub == "ui") return execUi(cmd, args);
    if (sub == "sys") return execSys(cmd, args);
    if (sub == "app") return execApp(cmd, args);
    if (sub == "log") return execLog(cmd, args);
    
    return Result::errInvalid("Unknown subsystem");
}

Result exec(const char* line) {
    // Parse: "subsystem cmd arg1 arg2 ..."
    // Supports quoted args: ui type editInput "Hello World"
    // Up to 16 tokens, split at spaces; a token that starts with " runs to the next "
    std::string_view rest = line;
    P::Array<P::String> tokens;
    while (tokens.size() < 16) {
        const size_t start = rest.find_first_not_of(' ');
        if (start == std::string_view::npos) break;
        rest.remove_prefix(start);
        const bool quoted = rest.front() == '"';
        if (quoted) rest.remove_prefix(1);
        const size_t end = rest.find(quoted ? '"' : ' ');
        tokens.emplace_back(rest.substr(0, end));
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);  // past the " or space
    }
    
    if (tokens.empty()) return Result::errInvalid("Empty command");
    
    // One token: a sys command ("ping")
    const bool sysOnly = tokens.size() == 1;
    const char* subsystem = sysOnly ? "sys" : tokens[0].c_str();
    const char* cmd = sysOnly ? tokens[0].c_str() : tokens[1].c_str();
    
    // Collect remaining args
    JsonDocument doc;
    JsonArray args = doc.to<JsonArray>();
    
    for (size_t i = sysOnly ? 1 : 2; i < tokens.size(); i++) {
        args.add(tokens[i].c_str());
    }
    
    return exec(subsystem, cmd, args);
}

} // namespace Console
