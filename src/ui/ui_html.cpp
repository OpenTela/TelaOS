/**
 * ui_html.cpp - HTML UI engine for LVGL
 * C++17 implementation with Core class
 */

#include "lvgl.h"
#include "ui/ui_html.h"
#include "core/core.h"
#include "core/sys_paths.h"
#include "ui/ui_html_internal.h"
#include "ui/ui_types.h"
#include "ui/html_parser.h"
#include "ui/css_parser.h"
#include "ui/dom.h"
#include "ui/ui_html_flow.h"
#include "ui/ui_tag_reader.h"
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
#include <string>
#include <string_view>

static const char *TAG = "ui_html";

// ============ CONSTANTS ============
namespace {

constexpr size_t MAX_PAGES_PER_GROUP = 4;
constexpr size_t MAX_PAGE_CONTENTS = 64;

constexpr size_t TAG_NAME_MAX = 31;  // a template's tag name is read up to this many characters
constexpr size_t VAR_NAME_LEN = 32;

// The first `what` in `text` at or after `from` (a pointer into it), or nullptr: strstr on a view
const char* findIn(std::string_view text, const char* from, std::string_view what) {
    const size_t at = text.find(what, static_cast<size_t>(from - text.data()));
    return at == std::string_view::npos ? nullptr : text.data() + at;
}

// The text from p up to end begins with `prefix`
bool startsWith(const char* p, const char* end, std::string_view prefix) {
    return p < end && std::string_view(p, static_cast<size_t>(end - p)).substr(0, prefix.size()) == prefix;
}

} // anonymous namespace

// Use utility namespaces
using namespace UI::StringUtils;
using namespace UI::XmlUtils;
using namespace UI::Tag;

// Global handlers (survive across apps — set once by ScriptManager)
void (*g_onclick_handler)(const char* func_name) = nullptr;
void (*g_ontap_handler)(const char* func_name, int x, int y) = nullptr;
void (*g_onhold_handler)(const char* func_name) = nullptr;
void (*g_onhold_xy_handler)(const char* func_name, int x, int y) = nullptr;
void (*g_onitem_handler)(const char* func_name, int idx1, const char* value) = nullptr;
void (*g_state_change_handler)(const char* var_name, const char* value) = nullptr;

// Freeze: defer UI binding updates, flush on unfreeze
static bool s_frozen = false;
static P::Map<P::String, P::String> s_frozenDirty;

static lv_obj_t *get_screen(void) {
    return lv_screen_active();
}

// ============ HEAD SECTION PARSING ============

// Define a state variable with type inference from YAML value
static void define_yaml_var(Store& store, const P::String& key, const P::String& val) {
    if (val.empty() || val == "\"\"" || val == "''") {
        store.defineString(key, "");
    } else if (val == "true" || val == "yes") {
        store.defineBool(key, true);
    } else if (val == "false" || val == "no") {
        store.defineBool(key, false);
    } else if (val.size() >= 2 &&
               ((val.front() == '"' && val.back() == '"') ||
                (val.front() == '\'' && val.back() == '\''))) {
        store.defineString(key, P::String(val.c_str() + 1, val.size() - 2));
    } else {
        char* numEnd = nullptr;
        long lv = strtol(val.c_str(), &numEnd, 10);
        if (numEnd && numEnd == val.c_str() + val.size()) {
            store.defineInt(key, (int)lv);
        } else {
            float fv = strtof(val.c_str(), &numEnd);
            if (numEnd && numEnd == val.c_str() + val.size()) {
                store.defineFloat(key, fv);
            } else {
                store.defineString(key, val);
            }
        }
    }
    LOG_D(Log::UI, "state(yaml): %s = %s", key.c_str(), val.c_str());
}

// Parsed YAML line
struct YamlLine {
    int indent;
    P::String key;
    P::String value;
    bool isArrayItem;
};

// Parse YAML state with nesting — flatten objects to dotted paths, arrays to Store arrays
static void parse_state_yaml(const P::String& text, Store& store) {
    P::Array<YamlLine> lines;
    const char* p = text.c_str();
    const char* end = p + text.size();
    
    while (p < end) {
        const char* lineStart = p;
        while (p < end && *p != '\n') p++;
        P::String line(lineStart, p - lineStart);
        if (p < end) p++;
        
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t firstNS = line.find_first_not_of(" \t");
        if (firstNS == P::String::npos || line[firstNS] == '#') continue;
        
        // Strip inline comments
        bool inQ = false;
        for (size_t i = firstNS; i < line.size(); i++) {
            if (line[i] == '"' || line[i] == '\'') inQ = !inQ;
            if (!inQ && line[i] == '#' && i > 0 && line[i-1] == ' ') {
                line = line.substr(0, i);
                break;
            }
        }
        
        P::String trimmed = line.substr(firstNS);
        YamlLine yl;
        yl.indent = (int)firstNS;
        yl.isArrayItem = false;
        
        if (trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == ' ') {
            yl.isArrayItem = true;
            P::String valPart = trimmed.substr(2);
            size_t vs = valPart.find_first_not_of(" \t");
            yl.value = (vs != P::String::npos) ? valPart.substr(vs) : "";
        } else {
            size_t colonPos = trimmed.find(':');
            if (colonPos == P::String::npos) continue;
            yl.key = trimmed.substr(0, colonPos);
            size_t ke = yl.key.find_last_not_of(" \t");
            if (ke != P::String::npos) yl.key = yl.key.substr(0, ke + 1);
            P::String valPart = trimmed.substr(colonPos + 1);
            size_t vs = valPart.find_first_not_of(" \t");
            yl.value = (vs != P::String::npos) ? valPart.substr(vs) : "";
        }
        lines.push_back(yl);
    }
    
    struct Level {
        int indent;
        P::String prefix;
        bool isArray;
        P::Array<P::String> arrayItems;
    };
    P::Array<Level> stack;
    stack.push_back({-1, "", false, {}});
    
    auto flushArray = [&](Level& lv) {
        if (lv.isArray && !lv.arrayItems.empty()) {
            store.defineArray(lv.prefix, lv.arrayItems);
        }
    };
    
    for (size_t i = 0; i < lines.size(); i++) {
        const auto& yl = lines[i];
        while (stack.size() > 1 && yl.indent <= stack.back().indent) {
            flushArray(stack.back());
            stack.pop_back();
        }
        P::String prefix = stack.back().prefix;
        
        if (yl.isArrayItem) {
            stack.back().isArray = true;
            P::String val = yl.value;
            if (val.size() >= 2 &&
                ((val.front() == '"' && val.back() == '"') ||
                 (val.front() == '\'' && val.back() == '\''))) {
                val = val.substr(1, val.size() - 2);
            }
            stack.back().arrayItems.push_back(val);
        } else if (!yl.value.empty()) {
            P::String fullKey = prefix.empty() ? yl.key : P::String(prefix + "." + yl.key);
            // Flow-style array: key: [a, "b", 'c']. Items may be quoted
            // (either style) or bare; bare items are trimmed. Empty
            // brackets ⇒ empty array. Without this branch the whole
            // bracket expression would be stored as a scalar string.
            const P::String& v = yl.value;
            if (v.front() == '[') {
                P::Array<P::String> items;
                const char* fp = v.c_str() + 1;
                const char* fe = v.c_str() + v.size();
                auto skipWs = [&]() { while (fp < fe && (*fp==' '||*fp=='\t')) fp++; };
                while (fp < fe) {
                    skipWs();
                    if (fp < fe && *fp == ']') break;
                    if (fp >= fe) break;
                    P::String item;
                    if (*fp == '"' || *fp == '\'') {
                        char q = *fp++;
                        while (fp < fe && *fp != q) {
                            if (*fp == '\\' && fp + 1 < fe) { item += fp[1]; fp += 2; }
                            else item += *fp++;
                        }
                        if (fp < fe) fp++;  // closing quote
                    } else {
                        const char* is = fp;
                        while (fp < fe && *fp != ',' && *fp != ']') fp++;
                        const char* ie = fp;
                        while (ie > is && (ie[-1]==' '||ie[-1]=='\t')) ie--;
                        item.assign(is, (size_t)(ie - is));
                    }
                    items.push_back(item);
                    skipWs();
                    if (fp < fe && *fp == ',') { fp++; continue; }
                    if (fp < fe && *fp == ']') break;
                }
                store.defineArray(fullKey, items);
            } else {
                define_yaml_var(store, fullKey, yl.value);
            }
        } else {
            P::String newPrefix = prefix.empty() ? yl.key : P::String(prefix + "." + yl.key);
            stack.push_back({yl.indent, newPrefix, false, {}});
        }
    }
    while (stack.size() > 0) {
        flushArray(stack.back());
        stack.pop_back();
    }
}

// Check OS version: os="2.0" → true
static bool isOsV2() {
    const auto& os = g_core.app().appOsRequirement();
    return !os.empty() && os[0] == '2';
}

// v1: Parse XML variables from ParsedElement into Store
static void parse_vars_to_store(const UI::ParsedElement& section, Store& store) {
    for (const auto& var : section.children) {
        auto name = var.get("name");
        if (name.empty()) continue;
        P::String nameStr(name);
        if (var.is("bool"))        store.defineBool(nameStr, var.getBool("default"));
        else if (var.is("int"))    store.defineInt(nameStr, var.getInt("default"));
        else if (var.is("float"))  store.defineFloat(nameStr, var.getFloat("default"));
        else if (var.is("string")) store.defineString(nameStr, P::String(var.get("default")));
        else if (var.is("array")) {
            // <array name="files" default="['a','b','c']"/>
            //
            // Bracketed list of quoted strings. Quotes may be either ' or "
            // so authors keep the outer XML attribute in the usual double
            // quotes and pick the inner style that fits. Missing/empty
            // default ⇒ empty array. Inside a quoted item, \\ \' \" \n \r \t
            // are recognised escapes; anything else is literal.
            P::Array<P::String> items;
            auto def = var.get("default");
            const char* p = def.data();
            const char* end = p + def.size();
            auto skipWs = [&]() {
                while (p < end && (*p==' '||*p=='\t'||*p=='\n'||*p=='\r')) p++;
            };
            skipWs();
            if (p < end && *p == '[') {
                p++;
                while (p < end) {
                    skipWs();
                    if (p < end && *p == ']') { p++; break; }
                    if (p >= end) break;
                    char q = *p;
                    if (q != '\'' && q != '"') break;  // malformed; stop here
                    p++;
                    P::String item;
                    while (p < end && *p != q) {
                        if (*p == '\\' && p + 1 < end) {
                            char e2 = p[1];
                            switch (e2) {
                                case 'n':  item += '\n'; break;
                                case 'r':  item += '\r'; break;
                                case 't':  item += '\t'; break;
                                case '\\': item += '\\'; break;
                                case '\'': item += '\''; break;
                                case '"':  item += '"';  break;
                                default:   item += e2;   break;
                            }
                            p += 2;
                        } else {
                            item += *p++;
                        }
                    }
                    if (p < end && *p == q) p++;
                    items.push_back(item);
                    skipWs();
                    if (p < end && *p == ',') { p++; continue; }
                    if (p < end && *p == ']') { p++; break; }
                }
            }
            store.defineArray(nameStr, items);
        }
    }
}

// Parse <state> — v1: XML tags, v2: YAML text
static void parse_state(const UI::ParsedElement& root) {
    auto state = root.find("state");
    if (!state) return;
    
    LOG_D(Log::UI, "state:");
    if (isOsV2()) {
        if (!state->text.empty())
            parse_state_yaml(state->text, g_core.store());
    } else {
        parse_vars_to_store(*state, g_core.store());
    }
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
            g_core.app().addTimer(std::move(timer));
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
    
    {
        P::String lang_str(script->get("language", "lua"));
        for (char& ch : lang_str) ch = tolower((unsigned char)ch);
        g_core.app().setScriptLang(lang_str);
    }

    // Get script content (text inside <script>...</script>)
    {
        P::String code = script->text;
        LOG_D(Log::UI, "script raw text: %d bytes", (int)code.size());
        // Trim leading whitespace
        size_t start = code.find_first_not_of(" \t\n\r");
        if (start != P::String::npos && start > 0) code.erase(0, start);
        g_core.app().setScriptCode(code);
    }
    
    LOG_D(Log::UI, "script: %s (%d bytes)", g_core.app().scriptLang().c_str(), (int)g_core.app().scriptCode().size());
    
    // Debug: show first 100 chars
    if (g_core.app().scriptCode().size() > 0) {
        auto preview = g_core.app().scriptCode().substr(0, 100);
        LOG_D(Log::UI, "script preview: %.100s...", preview.c_str());
    }
}

// <style> and <link rel="stylesheet"> of <app>, in their order: of two rules alike, the later one
// wins, as in a browser. Of the <style> sections, the first one counts, as before
static void parse_style(const UI::ParsedElement& root) {
    bool styled = false;
    for (const UI::ParsedElement& child : root.children) {
        if (child.is("link")) {
            HtmlFlow::linkStylesheet(child.get("rel"), child.get("href"), child.get("media"),
                                     child.has("disabled"));
            continue;
        }
        if (!child.is("style") || styled)
            continue;
        styled = true;
        if (!child.text.empty()) {
            UI::Css::instance().parse(child.text);
            LOG_D(Log::UI, "style: parsed CSS");
        }
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
            }
            
            freeDram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            if (freeDram < BLE_REQUIRED) {
                LOG_E(Log::UI, "system: NOT ENOUGH MEMORY FOR BLE! Need %u, have %u", BLE_REQUIRED, freeDram);
                g_core.store().set("_error", P::String("BLE: not enough memory"));
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

// ============ TEMPLATE & @FOR EXPANSION ============

// Parse <templates> section from raw HTML
// Helper: trim body, lowercase key, store template
static void store_template(const P::String& id, const char* bodyStart, const char* bodyEnd) {
    while (bodyStart < bodyEnd && isspace((unsigned char)*bodyStart)) bodyStart++;
    const char* trimEnd = bodyEnd;
    while (trimEnd > bodyStart && isspace((unsigned char)*(trimEnd - 1))) trimEnd--;
    
    P::String key = id;
    for (char& c : key) c = tolower((unsigned char)c);
    g_core.app().setTemplate(key, P::String(bodyStart, trimEnd - bodyStart));
    LOG_I(Log::UI, "template: %s -> %d bytes", id.c_str(), (int)(trimEnd - bodyStart));
}

static void parse_templates(const char* html) {
    const std::string_view text(html);
    const char* tplSection = findIn(text, html, "<templates>");
    if (!tplSection) tplSection = findIn(text, html, "<templates ");
    if (!tplSection) return;
    
    const char* sectionEnd = findIn(text, tplSection, "</templates>");
    if (!sectionEnd) return;
    
    if (!isOsV2()) {
        // v1: <template id="Name">body</template>
        const char* p = tplSection;
        while (p < sectionEnd) {
            const char* tstart = findIn(text, p, "<template ");
            if (!tstart || tstart >= sectionEnd) break;
            
            const char* astart = tstart + 10;
            const char* aend = astart;
            while (aend < sectionEnd && *aend != '>') aend++;
            if (aend >= sectionEnd) break;
            
            P::String id = getAttr(astart, aend, "id");
            if (id.empty() || !isupper((unsigned char)id[0])) {
                p = aend + 1;
                continue;
            }
            
            const char* bodyStart = aend + 1;
            const char* bodyEnd = findIn(text, bodyStart, "</template>");
            if (!bodyEnd || bodyEnd > sectionEnd) break;
            
            store_template(id, bodyStart, bodyEnd);
            p = bodyEnd + 11;
        }
    } else {
        // v2: <PascalCase>body</PascalCase>
        const char* p = tplSection + 11;
        while (p < sectionEnd) {
            while (p < sectionEnd && isspace((unsigned char)*p)) p++;
            if (p >= sectionEnd || *p != '<') { p++; continue; }
            if (p + 1 < sectionEnd && (p[1] == '/' || p[1] == '!')) {
                while (p < sectionEnd && *p != '>') p++;
                if (p < sectionEnd) p++;
                continue;
            }
            
            p++;
            P::String tag;
            while (p < sectionEnd && !isspace((unsigned char)*p) && *p != '>' && *p != '/' && tag.size() < TAG_NAME_MAX) {
                tag += *p++;
            }
            if (tag.empty() || !isupper((unsigned char)tag[0])) {
                while (p < sectionEnd && *p != '>') p++;
                if (p < sectionEnd) p++;
                continue;
            }
            
            while (p < sectionEnd && *p != '>') p++;
            if (p >= sectionEnd) break;
            p++;
            
            const P::String closePat = "</" + tag + ">";
            const char* bodyStart = p;
            const char* bodyEnd = findIn(text, p, closePat);
            if (!bodyEnd || bodyEnd > sectionEnd) break;
            
            store_template(tag, bodyStart, bodyEnd);
            p = bodyEnd + closePat.size();
        }
    }
    
    LOG_I(Log::UI, "templates: %d defined", g_core.app().templatesCount());
}

// Substitute {varname} with value in text, handling nested {val_{var}_{var2}}
// Simple text replacement: all occurrences of {key} -> val
static P::String substitute_var(const P::String& text, const P::String& var, const P::String& val) {
    P::String result;
    P::String pattern = P::String("{") + var + "}";
    size_t patLen = pattern.size();
    size_t pos = 0;
    
    while (pos < text.size()) {
        size_t found = text.find(pattern.c_str(), pos);
        if (found == P::String::npos) {
            result.append(text.c_str() + pos, text.size() - pos);
            break;
        }
        result.append(text.c_str() + pos, found - pos);
        result += val;
        pos = found + patLen;
    }
    return result;
}

// Expand a template invocation: substitute attributes into body
static P::String expand_template_call(const P::String& body, 
                                       const char* astart, const char* aend) {
    P::String result = body;
    
    // Parse attributes and substitute each as {name} -> value
    const char* p = astart;
    while (p < aend) {
        while (p < aend && isspace((unsigned char)*p)) p++;
        if (p >= aend || *p == '/' || *p == '>') break;
        
        // Read attr name
        const char* nstart = p;
        while (p < aend && *p != '=' && !isspace((unsigned char)*p) && *p != '/' && *p != '>') p++;
        P::String attrName(nstart, p - nstart);
        // Lowercase the attr name for matching
        for (char& c : attrName) c = tolower((unsigned char)c);
        
        if (attrName.empty()) { p++; continue; }
        
        // Skip =
        while (p < aend && isspace((unsigned char)*p)) p++;
        if (p >= aend || *p != '=') continue;
        p++; // skip =
        while (p < aend && isspace((unsigned char)*p)) p++;
        
        // Read value (quoted)
        P::String attrVal;
        if (p < aend && (*p == '"' || *p == '\'')) {
            char quote = *p++;
            const char* vstart = p;
            while (p < aend && *p != quote) p++;
            attrVal.assign(vstart, p - vstart);
            if (p < aend) p++; // skip closing quote
        }
        
        if (!attrName.empty()) {
            result = substitute_var(result, attrName, attrVal);
        }
    }
    return result;
}

// Find matching } for an opening { using depth tracking
static const char* find_matching_brace(const char* start, const char* end) {
    int depth = 1;
    const char* p = start;
    while (p < end && depth > 0) {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        if (depth > 0) p++;
    }
    return (depth == 0) ? p : nullptr;
}

// Parse and expand @for(var in start..end [step N]) { body }
// Returns expanded HTML, advances *pp past the directive
static P::String expand_for(const char* p, const char* end, const char** pp) {
    // p points to '@' in @for(...)
    p++; // skip @
    
    // Expect "for("
    if (!startsWith(p, end, "for(")) {
        *pp = p;
        return "";
    }
    p += 4; // skip "for("
    
    // Parse variable name
    while (p < end && isspace((unsigned char)*p)) p++;
    const char* varStart = p;
    while (p < end && isalnum((unsigned char)*p) && *p != ' ') p++;
    P::String varName(varStart, p - varStart);
    
    // Expect "in"
    while (p < end && isspace((unsigned char)*p)) p++;
    if (!startsWith(p, end, "in")) {
        LOG_E(Log::UI, "@for: expected 'in' after variable '%s'", varName.c_str());
        *pp = p;
        return "";
    }
    p += 2;
    while (p < end && isspace((unsigned char)*p)) p++;
    
    // Parse start..end
    int rangeStart = 0, rangeEnd = 0, step = 1;
    rangeStart = atoi(p);
    while (p < end && (isdigit((unsigned char)*p) || *p == '-')) p++;
    
    // Expect ".."
    if (p + 1 < end && p[0] == '.' && p[1] == '.') {
        p += 2;
    } else {
        LOG_E(Log::UI, "@for: expected '..' in range");
        *pp = p;
        return "";
    }
    
    rangeEnd = atoi(p);
    while (p < end && (isdigit((unsigned char)*p) || *p == '-')) p++;
    
    // Optional "step N"
    while (p < end && isspace((unsigned char)*p)) p++;
    if (startsWith(p, end, "step")) {
        p += 4;
        while (p < end && isspace((unsigned char)*p)) p++;
        step = atoi(p);
        while (p < end && isdigit((unsigned char)*p)) p++;
        if (step <= 0) step = 1;
    }
    
    // Skip to closing )
    while (p < end && *p != ')') p++;
    if (p < end) p++; // skip )
    
    // Skip whitespace to opening {
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || *p != '{') {
        LOG_E(Log::UI, "@for: expected '{' after @for(...)");
        *pp = p;
        return "";
    }
    p++; // skip opening {
    
    // Find matching }
    const char* bodyStart = p;
    const char* bodyEnd = find_matching_brace(p, end);
    if (!bodyEnd) {
        LOG_E(Log::UI, "@for: no matching '}' found");
        *pp = end;
        return "";
    }
    
    P::String body(bodyStart, bodyEnd - bodyStart);
    *pp = bodyEnd + 1; // past closing }
    
    // Check conflict with state
    if (g_core.store().has(varName)) {
        LOG_E(Log::UI, "@for: variable '%s' conflicts with state variable. Rename one of them.", 
                 varName.c_str());
        return "";
    }
    
    LOG_D(Log::UI, "@for(%s in %d..%d step %d) body=%d bytes", 
             varName.c_str(), rangeStart, rangeEnd, step, (int)body.size());
    
    // Expand: substitute {var} for each value
    P::String result;
    for (int i = rangeStart; i <= rangeEnd; i += step) {
        P::String expanded = substitute_var(body, varName, P::String(std::to_string(i)));
        result += expanded;
    }
    
    return result;
}

// Pre-process HTML: expand all @for directives
static P::String expand_all_for(const P::String& html) {
    P::String result;
    const char* p = html.c_str();
    const char* end = p + html.size();
    
    while (p < end) {
        // Look for @for
        if (*p == '@' && p + 4 < end && startsWith(p + 1, end, "for(")) {
            const char* next = nullptr;
            P::String expanded = expand_for(p, end, &next);
            result += expanded;
            p = next;
        } else {
            result += *p++;
        }
    }
    return result;
}

// Pre-process HTML: expand all template invocations (PascalCase tags)
static P::String expand_all_templates(const P::String& html) {
    if (!g_core.app().hasTemplates()) return html;
    
    P::String result;
    const char* p = html.c_str();
    const char* end = p + html.size();
    
    while (p < end) {
        if (*p == '<' && p + 1 < end && isupper((unsigned char)p[1])) {
            // Potential template call
            const char* tagStart = p + 1;
            const char* t = tagStart;
            
            // Read tag name
            P::String tag;
            while (t < end && !isspace((unsigned char)*t) && *t != '>' && *t != '/' && tag.size() < TAG_NAME_MAX) {
                tag += static_cast<char>(tolower((unsigned char)*t++));
            }
            
            // Look up template
            if (const P::String* tpl = g_core.app().findTemplate(tag)) {
                // Found template — parse attributes (skip quoted values)
                const char* astart = t;
                while (t < end && *t != '>') {
                    if (*t == '"') {
                        t++;
                        while (t < end && *t != '"') t++;
                        if (t < end) t++;
                    } else if (*t == '\'') {
                        t++;
                        while (t < end && *t != '\'') t++;
                        if (t < end) t++;
                    } else if (*t == '/' && t + 1 < end && *(t + 1) == '>') {
                        break;  // self-close
                    } else {
                        t++;
                    }
                }
                const char* aend = t;
                
                bool selfClose = (t > p && *t == '/' && t + 1 < end && *(t + 1) == '>');
                if (selfClose) {
                    t += 2; // skip />
                } else if (*t == '>') {
                    t++; // skip >
                    // Find closing tag </TagName>, its name in any case
                    const char* closePos = nullptr;
                    const char* sp = t;
                    while (sp < end) {
                        if (*sp == '<' && *(sp + 1) == '/') {
                            // Compare tag name case-insensitively
                            const char* cp = sp + 2;
                            size_t matched = 0;
                            while (matched < tag.size() && cp < end && tolower((unsigned char)*cp) == tag[matched]) {
                                cp++; matched++;
                            }
                            if (matched == tag.size() && cp < end && *cp == '>') {
                                closePos = cp + 1;
                                break;
                            }
                        }
                        sp++;
                    }
                    if (closePos) {
                        t = closePos;
                    }
                }
                
                // Expand template
                P::String expanded = expand_template_call(*tpl, astart, aend);
                result += expanded;
                p = t;
                continue;
            }
        }
        result += *p++;
    }
    return result;
}

// Full pre-expansion: @for first, then templates, repeat until stable
static P::String preprocess_html(const char* html, int len) {
    P::String current(html, len);
    
    // Max iterations to prevent infinite loops
    for (int pass = 0; pass < 8; pass++) {
        P::String after_for = expand_all_for(current);
        P::String after_tpl = expand_all_templates(after_for);
        
        if (after_tpl == current) break; // stable
        current = after_tpl;
    }
    
    return current;
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
        g_core.app().setAppVersion(P::String(ver));
        LOG_D(Log::UI, "App version: %s", g_core.app().appVersion().c_str());
    }
    
    // Read OS requirement
    auto os = app->get("os");
    if (!os.empty()) {
        g_core.app().setAppOsRequirement(P::String(os));
        LOG_D(Log::UI, "App requires OS: %s", g_core.app().appOsRequirement().c_str());
    }
    
    // Read readonly flag
    auto ro = app->get("readonly");
    g_core.app().setAppReadonly(ro == "true" || ro == "1");
    if (g_core.app().appReadonly()) {
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
            g_core.app().setAppIcon(fullPath.c_str());
        } else {
            g_core.app().setAppIcon(P::String(icon).c_str());
        }
        LOG_D(Log::UI, "App icon: %s", g_core.app().appIcon().c_str());
    }
    
    parse_state(*app);
    parse_templates(html);  // after state, so @for can check conflicts
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
            g_core.app().setDefaultPage(P::String(def));
            LOG_I(Log::UI, "UI default page: %s", g_core.app().defaultPage().c_str());
        }
    }
}

// ============ END HEAD PARSING ============

// CommonAttrs, parseCommonAttrs, ensureId, extractBindVar moved to ui_widget_builder.cpp

bool ui_trigger_click(const char *id) {
    for (const auto& el : g_core.app().elements) {
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
    lv_obj_t *obj = g_core.app().findElement(id);
    if (!obj) return;
    
    // TODO: Наши шрифты поддерживают только ASCII + кириллицу.
    // Нужно сделать warning или горячую замену Unicode символов.
    if (HtmlFlow::setText(obj, text)) return;  // HTML text with spans, blocks, checkboxes
    lv_obj_t *label = (lv_obj_get_child_cnt(obj) > 0) ? lv_obj_get_child(obj, 0) : obj;
    // Only a label takes text: a slider, bar or image has none (and must not be written as one)
    if (!lv_obj_check_type(label, &lv_label_class)) return;
    lv_label_set_text(label, text);
}

void ui_set_switch(const char *id, bool checked) {
    lv_obj_t *obj = g_core.app().findElement(id);
    if (!obj) return;
    if (checked) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
}

void ui_set_slider(const char *id, int value) {
    lv_obj_t *obj = g_core.app().findElement(id);
    if (!obj) return;
    lv_slider_set_value(obj, value, LV_ANIM_ON);
}

void ui_set_input(const char *id, const char *text) {
    lv_obj_t *obj = g_core.app().findElement(id);
    if (!obj) return;
    lv_textarea_set_text(obj, text ? text : "");
}

// Get state variable value by name - uses StateStore
const char *get_state_value(const char *name) {
    return state_store_get(name);
}

// Resolve a binding path: dotted keys and array[N] access
static const char* resolve_binding(const P::String& raw) {
    static P::String s_buf;
    auto& store = g_core.store();
    
    // Array access: "name[idx]"
    size_t bracket = raw.find('[');
    if (bracket != P::String::npos) {
        size_t closeBracket = raw.find(']', bracket);
        if (closeBracket != P::String::npos) {
            P::String arrayName = raw.substr(0, bracket);
            int idx = atoi(raw.substr(bracket + 1, closeBracket - bracket - 1).c_str());
            if (store.hasArray(arrayName)) {
                s_buf = store.getArrayItem(arrayName, idx);
                return s_buf.c_str();
            }
        }
    }
    
    // Scalar (flat or dotted)
    if (store.has(raw)) {
        s_buf = store.getAsString(raw);
        return s_buf.c_str();
    }
    return "";
}

// Set state variable value - uses StateStore (silent - no UI callback)
void ui_set_state(const char *name, const char *value) {
    state_store_set_silent(name, value);
}

// Render template: replace {var} with state values
// Supports dotted paths {player.x} and array access {items[0]}
P::String render_template(const char *tpl) {
    P::String result;
    const std::string_view text(tpl);
    size_t i = 0;
    
    while (i < text.size()) {
        if (text[i] == '{') {
            const size_t close = text.find('}', i);
            if (close != std::string_view::npos && close - i < VAR_NAME_LEN) {
                P::String varname(text.substr(i + 1, close - i - 1));
                result += resolve_binding(varname);
                i = close + 1;
            } else {
                result += text[i++];
            }
        } else {
            result += text[i++];
        }
    }
    return decodeEntities(result);
}

// Check if template contains {varname}
static bool template_has_var(std::string_view tpl, const char *varname) {
    if (!varname || !varname[0]) return false;
    const std::string_view name(varname);
    for (size_t at = tpl.find(name); at != std::string_view::npos; at = tpl.find(name, at + 1)) {
        if (at > 0 && tpl[at - 1] == '{' && at + name.size() < tpl.size() && tpl[at + name.size()] == '}') return true;
    }
    return false;
}

// Update all g_core.app().elements that bind to this variable
// Forward declarations
static void ui_update_bindings_internal(const char *varname, const char *value);
uint32_t parse_color(const char *s);

// Public function - defers if frozen, else applies immediately
void ui_update_bindings(const char *varname, const char *value) {
    if (s_frozen) {
        s_frozenDirty[P::String(varname)] = P::String(value);
        // Still update internal state so Lua reads current values
        ui_set_state(varname, value);
        return;
    }
    ui_update_bindings_internal(varname, value);
}

void ui_freeze() {
    s_frozen = true;
    s_frozenDirty.clear();
}

void ui_unfreeze() {
    if (!s_frozen) return;
    s_frozen = false;
    // Flush all deferred updates
    auto dirty = std::move(s_frozenDirty);
    for (auto& [name, value] : dirty) {
        ui_update_bindings_internal(name.c_str(), value.c_str());
    }
}

// Internal function that actually updates bindings
static void ui_update_bindings_internal(const char *varname, const char *value) {
    LOG_V(Log::UI, "update_bindings: var=%s val=%s elements=%d", varname, value, (int)g_core.app().elements.size());
    
    // First update internal state
    ui_set_state(varname, value);
    
    // Prevent recursion
    g_core.app().beginBinding();
    
    // Then find and update all g_core.app().elements
    for (size_t i = 0; i < g_core.app().elements.size(); i++) {
        if (!g_core.app().elements[i]) {
            continue;
        }
        
        lv_obj_t *obj = g_core.app().elements[i]->obj();
        if (!obj) {
            continue;
        }
        
        // Update templates with {varname}
        if (template_has_var(g_core.app().elements[i]->tpl.c_str(), varname)) {
            LOG_V(Log::UI, "update_bindings: updating element %d tpl=%s", (int)i, g_core.app().elements[i]->tpl.c_str());
            P::String rendered = render_template(g_core.app().elements[i]->tpl.c_str());
            LOG_V(Log::UI, "update_bindings: rendered=%s", rendered.c_str());
            
            // Custom update handler (e.g. markdown spangroup)
            if (g_core.app().elements[i]->updateFn) {
                g_core.app().elements[i]->updateFn(rendered);
            }
            else if (lv_obj_get_child_cnt(obj) > 0) {
                lv_obj_t *child = lv_obj_get_child(obj, 0);
                if (child) {
                    const char* cur = lv_label_get_text(child);
                    if (!cur || std::string_view(cur) != rendered.c_str()) {
                        lv_label_set_text(child, rendered.c_str());
                    }
                }
            } else {
                const char* cur = lv_label_get_text(obj);
                if (!cur || std::string_view(cur) != rendered.c_str()) {
                    lv_label_set_text(obj, rendered.c_str());
                }
            }
        }
        
        // Update dynamic class with {varname}
        if (template_has_var(g_core.app().elements[i]->classTemplate.c_str(), varname)) {
            P::String renderedClass = render_template(g_core.app().elements[i]->classTemplate.c_str());
            
            // For buttons: apply g_core.app().styles to parentObj (the button), not obj (the label inside)
            lv_obj_t *styleObj = g_core.app().elements[i]->parentObj ? g_core.app().elements[i]->parentObj : obj;
            if (!styleObj) {
                continue;
            }
            
            // Save position and size before removing g_core.app().styles
            lv_coord_t x = lv_obj_get_x(styleObj);
            lv_coord_t y = lv_obj_get_y(styleObj);
            lv_coord_t w = lv_obj_get_width(styleObj);
            lv_coord_t h = lv_obj_get_height(styleObj);
            
            // Reset g_core.app().styles and reapply class
            lv_obj_remove_style_all(styleObj);
            Widget{styleObj}.applyStyle(renderedClass);
            
            // Restore position and size
            lv_obj_set_pos(styleObj, x, y);
            lv_obj_set_size(styleObj, w, h);
            
            // For buttons: also update text color on the label inside
            if (g_core.app().elements[i]->parentObj && g_core.app().elements[i]->obj()) {
                lv_obj_t *lbl = g_core.app().elements[i]->obj();
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
            
            LOG_V(Log::UI, "Dynamic class update: %s -> %s", g_core.app().elements[i]->classTemplate.c_str(), renderedClass.c_str());
        }
        
        // Update widgets with bind=varname
        if (!g_core.app().elements[i]->bind.empty() && g_core.app().elements[i]->bind == varname) {
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
                if (!current || std::string_view(current) != value) {
                    lv_textarea_set_text(obj, value);
                }
            } else if (g_core.app().elements[i]->is_dropdown) {
                // Reverse binding: find value in dropdownValues → set index
                const auto& values = g_core.app().elements[i]->dropdownValues;
                for (int vi = 0; vi < (int)values.size(); ++vi) {
                    if (values[vi] == value) {
                        uint16_t cur = lv_dropdown_get_selected(obj);
                        if (cur != (uint16_t)vi) {
                            lv_dropdown_set_selected(obj, (uint16_t)vi);
                        }
                        break;
                    }
                }
            } else if (g_core.app().elements[i]->is_list) {
                // Full-array assignment (state.files = {...}) notifies with the
                // bare array name: rebuild the whole list from the store.
                list_rebuild_from_array(obj, (int)i, g_core.app().elements[i]->bind);
            }
        }

        // Per-index array update for lists: varname arrives as "files[2]"
        // (0-based) from setArrayItem. Match the prefix against the bind name
        // and patch just that one button's text — no rebuild.
        if (g_core.app().elements[i]->is_list &&
            !g_core.app().elements[i]->bind.empty()) {
            const std::string_view name(varname);
            const size_t lb = name.find('[');
            if (lb != std::string_view::npos) {
                const P::String& bindName = g_core.app().elements[i]->bind;
                if (name.substr(0, lb) == std::string_view(bindName)) {
                    int idx0 = atoi(varname + lb + 1);
                    lv_obj_t* listObj = g_core.app().elements[i]->w.handle;
                    uint32_t cnt = lv_obj_get_child_cnt(listObj);
                    if (idx0 >= 0 && idx0 < (int)cnt) {
                        lv_obj_t* btn = lv_obj_get_child(listObj, idx0);
                        if (btn) lv_list_set_button_text(listObj, btn, value);
                    }
                }
            }
        }
        
        // Update visibility with visibleBind=varname
        if (!g_core.app().elements[i]->visibleBind.empty() && g_core.app().elements[i]->visibleBind == varname) {
            const std::string_view v(value);
            bool visible = (v == "true" || v == "1");
            UI::setVisible(g_core.app().elements[i]->box(), visible);
            LOG_V(Log::UI, "Visibility update: %s -> %s", g_core.app().elements[i]->id.c_str(), visible ? "visible" : "hidden");
        }

        // Update disabled state with disabled="{varname}"
        if (!g_core.app().elements[i]->disabledBind.empty() && g_core.app().elements[i]->disabledBind == varname) {
            const std::string_view v(value);
            bool disabled = (v == "true" || v == "1");
            lv_obj_t* obj_d = g_core.app().elements[i]->box().handle;
            if (obj_d) {
                if (disabled) lv_obj_add_state(obj_d, LV_STATE_DISABLED);
                else          lv_obj_remove_state(obj_d, LV_STATE_DISABLED);
            }
            LOG_V(Log::UI, "Disabled update: %s -> %s", g_core.app().elements[i]->id.c_str(), disabled ? "disabled" : "enabled");
        }
        
        // Update background color with bgcolorBind=varname
        if (!g_core.app().elements[i]->bgcolorBind.empty()) {
            LOG_V(Log::UI, "  element %s: bgcolorBind=%s (looking for %s)", 
                     g_core.app().elements[i]->id.c_str(), g_core.app().elements[i]->bgcolorBind.c_str(), varname);
        }
        if (!g_core.app().elements[i]->bgcolorBind.empty() && g_core.app().elements[i]->bgcolorBind == varname) {
            LOG_V(Log::UI, "Bgcolor update: %s -> %s", g_core.app().elements[i]->id.c_str(), value);
            UI::setBgColor(g_core.app().elements[i]->box(), parse_color(value));
        }
        
        // Update text color with colorBind=varname
        if (!g_core.app().elements[i]->colorBind.empty() && g_core.app().elements[i]->colorBind == varname) {
            UI::setColor(g_core.app().elements[i]->w, parse_color(value));
            LOG_V(Log::UI, "Color update: %s -> %s", g_core.app().elements[i]->id.c_str(), value);
        }
    }
    
    g_core.app().endBinding();
}

// Sync all widgets with their bound state values (call after loadState)
// Find group by id
static int find_group_index(const char *id) {
    return g_core.app().findGroup(id);
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

// html {} and body {} on a page, as a browser puts them on its canvas: the background (unless
// the page has its own bgcolor=""), square, and the color its text inherits, TelaOS widgets' and
// HTML's. After the page's content: a <style> or <link> in it counts too, as it does for what
// comes after it
static void apply_canvas(lv_obj_t* page, std::string_view attrs) {
    const UI::Css::Canvas c = UI::Css::instance().canvas();
    const bool own = !getAttr(attrs.data(), attrs.data() + attrs.size(), "bgcolor").empty();
    if (c.hasBackground && !own) {
        lv_obj_set_style_bg_color(page, lv_color_hex(c.background), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(page, c.backgroundOpa, LV_PART_MAIN);
        lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
    }
    if (c.hasColor) {
        lv_obj_set_style_text_color(page, lv_color_hex(c.color), LV_PART_MAIN);
        if (c.colorOpa < LV_OPA_COVER)
            lv_obj_set_style_text_opa(page, c.colorOpa, LV_PART_MAIN);
        HtmlFlow::setTextColor(page, c.color, c.colorOpa);
    }
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
    const std::string_view rest(p);
    const size_t slash = rest.find('/');
    
    if (slash != std::string_view::npos) {
        // Path is group/page
        P::String group_id(rest.substr(0, slash));
        P::String page_id(rest.substr(slash + 1));
        
        int grp_idx = find_group_index(group_id.c_str());
        if (grp_idx < 0) {
            LOG_W(Log::UI, "Group not found: %s", group_id.c_str());
            return;
        }
        
        int page_idx = find_page_in_group(&g_core.app().group(grp_idx), page_id.c_str());
        if (page_idx < 0) {
            LOG_W(Log::UI, "Page not found: %s/%s", group_id.c_str(), page_id.c_str());
            return;
        }
        
        // Hide all standalone pages
        for (int i = 0; i < g_core.app().pagesCount(); i++) {
            lv_obj_add_flag(g_core.app().pageObj(i), LV_OBJ_FLAG_HIDDEN);
        }
        
        // Hide all groups except this one
        for (int i = 0; i < g_core.app().groupsCount(); i++) {
            if (i == grp_idx) {
                lv_obj_clear_flag(g_core.app().group(i).tileview, LV_OBJ_FLAG_HIDDEN);
                if (g_core.app().group(i).indicator_obj) {
                    lv_obj_clear_flag(g_core.app().group(i).indicator_obj, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                lv_obj_add_flag(g_core.app().group(i).tileview, LV_OBJ_FLAG_HIDDEN);
                if (g_core.app().group(i).indicator_obj) {
                    lv_obj_add_flag(g_core.app().group(i).indicator_obj, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
        
        // Navigate to tile using tileview API
        lv_obj_set_tile_id(g_core.app().group(grp_idx).tileview, page_idx, 0, LV_ANIM_OFF);
        g_core.app().group(grp_idx).current_page_idx = page_idx;
        g_core.app().group(grp_idx).updateIndicator(page_idx);
        
        g_core.app().setCurrentGroup(grp_idx);
        g_core.app().setCurrentPage(INVALID_INDEX);
        
        LOG_I(Log::UI, "Navigate: %s/%s", group_id.c_str(), page_id.c_str());
    } else {
        // Path is just page_id - could be group or standalone page
        int grp_idx = find_group_index(p);
        if (grp_idx >= 0) {
            // It's a group - show default page
            const auto& def = !g_core.app().group(grp_idx).default_page.empty() 
                ? g_core.app().group(grp_idx).default_page : g_core.app().group(grp_idx).page_ids[0];
            ui_show_page_internal((g_core.app().group(grp_idx).id + "/" + def).c_str());
            return;
        }
        
        // It's a standalone page
        int idx = g_core.app().findPage(p);
        if (idx < 0) {
            LOG_W(Log::UI, "Page not found: %s", p);
            return;
        }
        
        // Hide all groups
        for (int i = 0; i < g_core.app().groupsCount(); i++) {
            lv_obj_add_flag(g_core.app().group(i).tileview, LV_OBJ_FLAG_HIDDEN);
            if (g_core.app().group(i).indicator_obj) {
                lv_obj_add_flag(g_core.app().group(i).indicator_obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        // Hide keyboards
        for (int i = 0; i < g_core.app().pagesCount(); i++) {
            if (g_core.app().keyboards[i]) {
                lv_obj_add_flag(g_core.app().keyboards[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        // Show/hide standalone pages
        for (int i = 0; i < g_core.app().pagesCount(); i++) {
            if (i == idx) {
                lv_obj_clear_flag(g_core.app().pageObj(i), LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(g_core.app().pageObj(i), LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        g_core.app().setCurrentGroup(INVALID_INDEX);
        g_core.app().setCurrentPage(idx);
        
        LOG_I(Log::UI, "Navigate: %s", p);
    }
}

void navigate(const char *href) {
    if (!href || href[0] != '/') return;
    LOG_I(Log::UI, "navigate: %s", href);
    const char *target = href + 1;
    const std::string_view to(target);
    
    if (to == "next") {
        if (g_core.app().currentPage() < g_core.app().pagesCount() - 1) {
            ui_show_page_internal(g_core.app().pageId(g_core.app().currentPage() + 1).c_str());
        }
    } else if (to == "prev" || to == "back") {
        if (g_core.app().currentPage() > 0) {
            ui_show_page_internal(g_core.app().pageId(g_core.app().currentPage() - 1).c_str());
        }
    } else {
        ui_show_page_internal(target);
    }
}

const char* ui_get_current_page_id() {
    if (g_core.app().currentGroup() >= 0 && g_core.app().currentGroup() < g_core.app().groupsCount()) {
        auto& g = g_core.app().group(g_core.app().currentGroup());
        if (g.current_page_idx >= 0 && g.current_page_idx < (int)g.page_ids.size())
            return g.page_ids[g.current_page_idx].c_str();
    }
    if (g_core.app().currentPage() >= 0 && g_core.app().currentPage() < g_core.app().pagesCount())
        return g_core.app().pageId(g_core.app().currentPage()).c_str();
    return "";
}

// Event handlers + widget builders moved to ui_widget_builder.cpp

// Forward declaration for parse_children now in ui_html_internal.h

// ============ Tabs widget ============
// <tabs id="t" x="0" y="0" w="100%" h="100%" barh="32">
//   <tab title="Edit">...widgets...</tab>
//   <tab title="Preview">...widgets...</tab>
// </tabs>

// When a scroll of the tabs ends, LVGL finds the tab it ended on by dividing by the width of their
// content (lv_tabview.c, cont_scroll_end_event_cb). Tabs laid out 0 wide (100% of a table cell as
// wide as its content) would divide by zero: an exception that restarts the watch. Before LVGL's
// handler, this one skips it then: with no room there is no tab to find
static void tabsWithoutRoom(lv_event_t* e) {
    lv_obj_t* cont = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
    if (lv_obj_get_content_width(cont) <= 0 || lv_obj_get_content_height(cont) <= 0) lv_event_stop_processing(e);
}

void create_tabs(const char* astart, const char* aend, const char* content, lv_obj_t* parent) {
    using namespace UI::XmlUtils;
    
    int32_t x = parse_coord_w(getAttr(astart, aend, "x").c_str());
    int32_t y = parse_coord_h(getAttr(astart, aend, "y").c_str());
    int32_t w = parse_size(getAttr(astart, aend, "w").c_str());
    int32_t h = parse_size(getAttr(astart, aend, "h").c_str());
    int barh = getAttrInt(astart, aend, "barh", 32);
    auto id = getAttr(astart, aend, "id");
    auto bgcolorAttr = getAttr(astart, aend, "bgcolor");
    auto colorAttr = getAttr(astart, aend, "color");
    
    // Create tabview
    lv_obj_t* tv = lv_tabview_create(parent);
    UI::Dom::element(tv, "tabs", std::string_view(astart, static_cast<size_t>(aend - astart)));
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
    if (cont) {
        lv_obj_add_event_cb(cont, tabsWithoutRoom, static_cast<lv_event_code_t>(LV_EVENT_SCROLL_END | LV_EVENT_PREPROCESS),
                            nullptr);
    }
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
    const std::string_view text(content);
    const char* p = content;
    const char* end = content + text.size();
    
    while (p < end) {
        const char* tstart = findIn(text, p, "<tab");
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
        const char* tab_close = findIn(text, ta, "</tab>");
        if (!tab_close || tab_close > end) break;
        
        // Create tab
        lv_obj_t* tab_page = lv_tabview_add_tab(tv, title.c_str());
        
        // Style tab page
        lv_obj_set_style_bg_opa(tab_page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(tab_page, 0, 0);
        lv_obj_set_style_border_width(tab_page, 0, 0);
        
        // Parse widgets inside this tab: for CSS they are inside <tabs> and this <tab>
        int tab_content_len = tab_close - ta;
        {
            UI::CssScope inTabs("tabs", std::string_view(astart, static_cast<size_t>(aend - astart)),
                                std::string_view(content, static_cast<size_t>(end - content)));
            UI::Dom::element(tab_page, "tab", std::string_view(tab_astart, static_cast<size_t>(tab_aend - tab_astart)));
            UI::CssScope inTab("tab", std::string_view(tab_astart, static_cast<size_t>(tab_aend - tab_astart)),
                               std::string_view(ta, static_cast<size_t>(tab_content_len)));
            parse_children(ta, tab_content_len, tab_page);
        }
        
        LOG_I(Log::UI, "tab: '%s' (%d bytes content)", title.c_str(), tab_content_len);
        
        p = tab_close + 6; // skip "</tab>"
    }
    
    LOG_I(Log::UI, "tabs: id=%s barh=%d", id.empty() ? "?" : id.c_str(), barh);
}

// ============ PAGE WALKER ============

namespace {

// @for directives and template calls (<Card .../>) are expanded before the walk
bool needsExpansion(std::string_view html) {
    for (size_t i = 0; i < html.size(); ++i) {
        if (html[i] == '@') return true;
        if (html[i] == '<' && i + 1 < html.size() && html[i + 1] >= 'A' && html[i + 1] <= 'Z') return true;
    }
    return false;
}

// Id for an element the author left without one, when the engine must find it later
P::String autoId(std::string_view prefix, int n) {
    P::String id(prefix);
    id += P::String(std::to_string(n));
    return id;
}

// <div>: a general container; flex= lays its children out in a row or a column
void build_div(const UI::StartTag& tag, const P::String& content, lv_obj_t* host, lv_obj_t* parent) {
    const UI::Attrs& a = tag.attrs;
    lv_obj_t *container = lv_obj_create(host);
    lv_obj_remove_style_all(container);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    auto wAttr = a.get("w");
    auto hAttr = a.get("h");
    auto xAttr = a.get("x");
    auto yAttr = a.get("y");
    auto bgAttr = a.get("bgcolor");
    auto flexAttr = a.get("flex");
    auto gapAttr = a.get("gap");
    auto justifyAttr = a.get("justify");
    auto alignAttr = a.get("align");
    auto radiusAttr = a.get("radius");
    auto paddingAttr = a.get("padding");
    auto overflowAttr = a.get("overflow");
    auto growAttr = a.get("grow");
    auto idAttr = a.get("id");
    auto classAttr = a.get("class");
    auto visAttr = a.get("visible");
    auto disAttr = a.get("disabled");

    lv_obj_set_width(container, LV_SIZE_CONTENT);
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    if (host != parent) lv_obj_set_width(container, lv_pct(100));  // in HTML flow: a block
    if (!wAttr.empty()) lv_obj_set_width(container, parse_size(wAttr.c_str()));
    if (!hAttr.empty()) lv_obj_set_height(container, parse_size(hAttr.c_str()));

    if (!xAttr.empty() || !yAttr.empty()) {
        int32_t xv = xAttr.empty() ? 0 : parse_coord_w(xAttr.c_str(), host);
        int32_t yv = yAttr.empty() ? 0 : parse_coord_h(yAttr.c_str(), host);
        lv_obj_set_pos(container, xv, yv);
    }

    // Background (with dynamic binding support)
    bool hasDynBg = !bgAttr.empty() && bgAttr.find('{') != P::String::npos;
    if (!bgAttr.empty()) {
        if (hasDynBg) {
            P::String varName = extractBindVar(bgAttr.c_str());
            P::String initVal = g_core.store().getString(varName);
            if (!initVal.empty()) {
                uint32_t bgc = parse_color(initVal.c_str());
                lv_obj_set_style_bg_color(container, lv_color_hex(bgc), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
            }
        } else {
            uint32_t bgc = parse_color(bgAttr.c_str());
            lv_obj_set_style_bg_color(container, lv_color_hex(bgc), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
        }
    }

    if (!radiusAttr.empty())
        lv_obj_set_style_radius(container, atoi(radiusAttr.c_str()), LV_PART_MAIN);
    if (!paddingAttr.empty())
        lv_obj_set_style_pad_all(container, parse_size(paddingAttr.c_str()), LV_PART_MAIN);
    if (overflowAttr == "scroll")
        lv_obj_add_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Flex layout
    bool stretchChildren = false;
    bool stretchIsWidth  = false;  // true=stretch child width (row), false=stretch height (column)
    if (!flexAttr.empty()) {
        lv_flex_flow_t flow = LV_FLEX_FLOW_ROW;
        if (flexAttr == "column")              flow = LV_FLEX_FLOW_COLUMN;
        else if (flexAttr == "row-wrap")       flow = LV_FLEX_FLOW_ROW_WRAP;
        else if (flexAttr == "column-wrap")    flow = LV_FLEX_FLOW_COLUMN_WRAP;
        else if (flexAttr == "row-reverse")    flow = LV_FLEX_FLOW_ROW_REVERSE;
        else if (flexAttr == "column-reverse") flow = LV_FLEX_FLOW_COLUMN_REVERSE;
        lv_obj_set_flex_flow(container, flow);
        // Cross axis = perpendicular to flow. So for row-flow flex,
        // children get stretched in HEIGHT; for column-flow, in WIDTH.
        const bool isColumnFlow =
            (flow == LV_FLEX_FLOW_COLUMN) ||
            (flow == LV_FLEX_FLOW_COLUMN_WRAP) ||
            (flow == LV_FLEX_FLOW_COLUMN_REVERSE);
        stretchIsWidth = isColumnFlow;  // column flow -> stretch width; row flow -> stretch height

        if (!gapAttr.empty()) {
            int32_t gap = parse_size(gapAttr.c_str());
            lv_obj_set_style_pad_row(container, gap, LV_PART_MAIN);
            lv_obj_set_style_pad_column(container, gap, LV_PART_MAIN);
        }

        lv_flex_align_t mainAlign = LV_FLEX_ALIGN_START;
        lv_flex_align_t crossAlign = LV_FLEX_ALIGN_START;
        if (!justifyAttr.empty()) {
            if (justifyAttr == "center")       mainAlign = LV_FLEX_ALIGN_CENTER;
            else if (justifyAttr == "end")     mainAlign = LV_FLEX_ALIGN_END;
            else if (justifyAttr == "between") mainAlign = LV_FLEX_ALIGN_SPACE_BETWEEN;
            else if (justifyAttr == "around")  mainAlign = LV_FLEX_ALIGN_SPACE_AROUND;
            else if (justifyAttr == "evenly")  mainAlign = LV_FLEX_ALIGN_SPACE_EVENLY;
        }
        if (!alignAttr.empty()) {
            if (alignAttr == "center")       crossAlign = LV_FLEX_ALIGN_CENTER;
            else if (alignAttr == "end")      crossAlign = LV_FLEX_ALIGN_END;
            else if (alignAttr == "stretch") {
                // LVGL's lv_flex_align_t has no STRETCH (verified against
                // lvgl/lvgl src/layouts/flex/lv_flex.h — only START/END/
                // CENTER/SPACE_*). To make align="stretch" actually do
                // something useful, we keep cross=START and stretch each
                // child to 100% on the cross axis after they are built.
                crossAlign = LV_FLEX_ALIGN_START;
                stretchChildren = true;
            }
        }
        lv_obj_set_flex_align(container, mainAlign, crossAlign, LV_FLEX_ALIGN_START);
    }

    if (!growAttr.empty() && !HtmlFlow::inFlow(host))  // no grow in an HTML flow box: see parse_children
        lv_obj_set_flex_grow(container, atoi(growAttr.c_str()));

    Widget{container}.applyCss("div", idAttr.c_str(), classAttr.c_str(), a.text());
    HtmlFlow::applyDir(container, a.get("dir").c_str());

    bool hasDynVisible = !visAttr.empty() && visAttr.find('{') != P::String::npos;
    bool hasDynDisabled = !disAttr.empty() && disAttr.find('{') != P::String::npos;
    if (!idAttr.empty() || hasDynVisible || hasDynDisabled || hasDynBg) {
        P::String id = idAttr;
        if (id.empty()) {
            static int s_divIdx = 0;
            id = autoId("_div_", s_divIdx++);
        }
        ElementDesc ed = {};
        ed.id = id.c_str();
        ed.obj = container;
        ed.visibleBind = hasDynVisible ? visAttr.c_str() : nullptr;
        ed.disabledBind = hasDynDisabled ? disAttr.c_str() : nullptr;
        ed.bgcolorBind = hasDynBg ? bgAttr.c_str() : nullptr;
        g_core.app().addElement(ed);
    }

    LOG_I(Log::UI, "<div> id=%s flex=%s", idAttr.empty() ? "-" : idAttr.c_str(),
             flexAttr.empty() ? "-" : flexAttr.c_str());

    if (!content.empty()) {
        UI::CssScope inside("div", a.text(), std::string_view(content.c_str(), content.length()));  // .card label {}
        // Direct text (<div>Hello</div>) used to be dropped; render it as HTML flow
        if (HtmlFlow::hasFlowContent(content.c_str(), content.length())) {
            if (flexAttr.empty()) HtmlFlow::makeFlow(container);
            HtmlFlow::renderContent(content.c_str(), content.length(), container);
        } else {
            parse_children(content.c_str(), content.length(), container);
        }
    }

    // align="stretch" emulation: LVGL flex has no STRETCH enum value,
    // so we set each child's cross-axis size to 100% of the container.
    if (stretchChildren) {
        uint32_t n = lv_obj_get_child_cnt(container);
        for (uint32_t i = 0; i < n; ++i) {
            lv_obj_t* ch = lv_obj_get_child(container, i);
            if (!ch) continue;
            if (stretchIsWidth)
                lv_obj_set_width(ch,  lv_pct(100));
            else
                lv_obj_set_height(ch, lv_pct(100));
        }
    }
}

// <table>, <tr>, <td>: a table is a column of rows, a row is a row of cells
void build_table_part(const UI::StartTag& tag, const P::String& content, lv_obj_t* host, lv_obj_t* parent) {
    using UI::TagId;
    const UI::Attrs& a = tag.attrs;
    lv_obj_t *container = lv_obj_create(host);
    lv_obj_remove_style_all(container);  // clean slate: no theme defaults

    // Flex direction: table=column, tr=row, td=none
    if (tag.id == TagId::Table) {
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    } else if (tag.id == TagId::Tr) {
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    }

    // Read attributes first (needed for default decisions)
    auto wAttr = a.get("w");
    auto hAttr = a.get("h");
    auto xAttr = a.get("x");
    auto yAttr = a.get("y");
    auto bgAttr = a.get("bgcolor");

    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    // Default size — HTML semantics:
    // table without h: rows size to content
    // table with h: rows share height via flex-grow
    static bool s_tableHasHeight = false;

    if (tag.id == TagId::Table) {
        // in HTML flow a table is a block; content-sized, its 100%-wide rows would get 0
        lv_obj_set_width(container, host != parent ? lv_pct(100) : LV_SIZE_CONTENT);
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        s_tableHasHeight = !hAttr.empty();
    } else if (tag.id == TagId::Tr) {
        lv_obj_set_width(container, lv_pct(100));
        if (s_tableHasHeight && hAttr.empty()) {
            lv_obj_set_flex_grow(container, 1);
        } else {
            lv_obj_set_height(container, LV_SIZE_CONTENT);
        }
    } else if (tag.id == TagId::Td) {
        // a cell takes its share of the row; colspan="2" takes two shares, as two cells would
        const int span = atoi(a.get("colspan").c_str());
        lv_obj_set_flex_grow(container, span > 1 ? span : 1);
        if (s_tableHasHeight && hAttr.empty()) {
            lv_obj_set_height(container, lv_pct(100));
        } else {
            lv_obj_set_height(container, LV_SIZE_CONTENT);
        }
    }

    // Explicit attributes (override defaults)
    if (!wAttr.empty()) lv_obj_set_width(container, parse_size(wAttr.c_str()));
    if (!hAttr.empty()) lv_obj_set_height(container, parse_size(hAttr.c_str()));

    if (!xAttr.empty() || !yAttr.empty()) {
        int32_t x = xAttr.empty() ? 0 : parse_coord_w(xAttr.c_str(), host);
        int32_t y = yAttr.empty() ? 0 : parse_coord_h(yAttr.c_str(), host);
        lv_obj_set_pos(container, x, y);
    }

    if (!bgAttr.empty()) {
        uint32_t bgc = parse_color(bgAttr.c_str());
        lv_obj_set_style_bg_color(container, lv_color_hex(bgc), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
    }

    // cellspacing (HTML classic) / gap (modern) — spacing between cells
    static int32_t s_tableCellspacing = 0;
    auto gapAttr = a.get("cellspacing");
    if (gapAttr.empty()) gapAttr = a.get("gap");
    int32_t gap = gapAttr.empty() ? 0 : parse_coord_w(gapAttr.c_str());

    if (tag.id == TagId::Table) {
        s_tableCellspacing = gap;  // store for child tr's
        if (gap > 0) {
            lv_obj_set_style_pad_row(container, gap, LV_PART_MAIN);
        }
    } else if (tag.id == TagId::Tr) {
        int colGap = (gap > 0) ? gap : s_tableCellspacing;
        if (colGap > 0) {
            lv_obj_set_style_pad_column(container, colGap, LV_PART_MAIN);
        }
    } else if (gap > 0) {
        // td with explicit gap
        lv_obj_set_style_pad_row(container, gap, LV_PART_MAIN);
        lv_obj_set_style_pad_column(container, gap, LV_PART_MAIN);
    }

    // Apply CSS (may override w, h, bgcolor)
    auto idAttr = a.get("id");
    auto classAttr = a.get("class");
    Widget{container}.applyCss(tag.name.c_str(), idAttr.c_str(), classAttr.c_str(), a.text());
    // <table dir="rtl">: columns from the right (rows are flex rows, which follow base_dir)
    HtmlFlow::applyDir(container, a.get("dir").c_str());

    // visible binding
    auto visAttr = a.get("visible");
    auto disAttr = a.get("disabled");
    bool hasDynVisible = !visAttr.empty() && visAttr.find('{') != P::String::npos;
    bool hasDynDisabled = !disAttr.empty() && disAttr.find('{') != P::String::npos;

    // Register element if it has id or dynamic visible
    if (!idAttr.empty() || hasDynVisible || hasDynDisabled) {
        // Auto-generate id if needed
        P::String id = idAttr;
        if (id.empty()) {
            static int s_containerIdx = 0;
            P::String prefix("_");
            prefix += tag.name;
            prefix += '_';
            id = autoId(prefix, s_containerIdx++);
        }

        ElementDesc ed = {};
        ed.id = id.c_str();
        ed.obj = container;
        ed.visibleBind = hasDynVisible ? visAttr.c_str() : nullptr;
        ed.disabledBind = hasDynDisabled ? disAttr.c_str() : nullptr;
        g_core.app().addElement(ed);
    }

    LOG_I(Log::UI, "<%s> id=%s class=%s vis=%s", tag.name.c_str(),
             idAttr.empty() ? "-" : idAttr.c_str(),
             classAttr.empty() ? "-" : classAttr.c_str(),
             visAttr.empty() ? "-" : visAttr.c_str());

    // Recursive parse children; direct cell text (<td>Moscow</td>) renders as HTML flow
    if (!content.empty()) {
        UI::CssScope inside(tag.name, a.text(), std::string_view(content.c_str(), content.length()));  // tr > td {}
        if (tag.id == TagId::Td &&
            HtmlFlow::hasFlowContent(content.c_str(), content.length())) {
            HtmlFlow::makeFlow(container);
            HtmlFlow::renderContent(content.c_str(), content.length(), container);
        } else {
            parse_children(content.c_str(), content.length(), container);
        }
    }
}

// One element of the page, by its tag
void build_element(const UI::StartTag& tag, const P::String& content, lv_obj_t* host, lv_obj_t* parent) {
    using UI::TagId;
    const char* const a = tag.attrs.begin();
    const char* const e = tag.attrs.end();
    const char* const c = content.c_str();
    switch (tag.id) {
        case TagId::Label:    create_label(a, e, c, host); break;
        case TagId::Button:   create_button(a, e, c, host); break;
        case TagId::Switch:   create_switch(a, e, host); break;
        case TagId::Slider:   create_slider(a, e, host); break;
        case TagId::Input:
            // <input type="checkbox|radio|range|submit|..."> follows HTML; without type= it is TelaOS input
            if (HtmlFlow::isHtmlInput(a, e)) {
                HtmlFlow::create(tag.name.c_str(), a, e, c, host);
            } else {
                create_input(a, e, c, host);
            }
            break;
        case TagId::Canvas:   create_canvas(a, e, host); break;
        case TagId::Image:    create_image(a, e, host); break;
        case TagId::Markdown: create_markdown(a, e, c, host); break;
        case TagId::Qr:       create_qr(a, e, c, host); break;
        case TagId::Tabs:     create_tabs(a, e, c, host); break;
        case TagId::Select:   create_select(a, e, c, host); break;
        case TagId::List:     create_list(a, e, c, host); break;
        case TagId::Div:      build_div(tag, content, host, parent); break;
        case TagId::Table:
        case TagId::Tr:
        case TagId::Td:       build_table_part(tag, content, host, parent); break;
        case TagId::Other:
            // Standard HTML elements (p, h1, ul, img, details...) — see ui_html_flow.h
            if (HtmlFlow::handles(tag.name.c_str())) HtmlFlow::create(tag.name.c_str(), a, e, c, host);
            break;
        case TagId::Page:
        case TagId::Tab:
            break;  // the walker passes over them
    }
}

}  // namespace

// Build the widgets of a page, or of a container's content, from its markup
void parse_children(const char *html, int len, lv_obj_t *parent) {
    using UI::TagId;
    using UI::TagReader;
    std::string_view page(html, static_cast<size_t>(len));
    P::String expanded;
    if (needsExpansion(page)) {
        expanded = preprocess_html(html, len);
        page = expanded;
        LOG_D(Log::UI, "preprocess: %d -> %d bytes", len, (int)expanded.size());
    }
    UI::CssScopeText readFrom(std::string_view(html, static_cast<size_t>(len)), page);  // li:first-child in the copy

    // HTML here (text, <p>, <ul>...): the container flows like a browser page, and its
    // TelaOS widgets without x/y/align take their place in that flow (see ui_html_flow.h)
    const bool flowHere = HtmlFlow::hasHtml(page.data(), page.size());

    TagReader reader(page);
    UI::StartTag tag;
    while (!reader.done()) {
        // Direct text and inline HTML (<b>, <a>, <br>...) form one line box, as in a browser
        if (flowHere) {
            const std::string_view rest = reader.rest();
            const char *run = HtmlFlow::inlineRun(rest.data(), rest.data() + rest.size(), parent);
            if (run != rest.data()) {
                reader.skip(static_cast<size_t>(run - rest.data()));
                continue;
            }
        }
        const TagReader::Step step = reader.next(tag);
        if (step == TagReader::Step::End) break;
        if (step == TagReader::Step::Skip) continue;

        LOG_I(Log::UI, "parse_tag: [%s]", tag.name.c_str());

        // Nested pages and tabs: the engine builds pages, <tabs> builds its tabs
        if (tag.id == TagId::Page || tag.id == TagId::Tab) continue;

        const P::String content(reader.content(tag));
        lv_obj_t *const host =
            (flowHere && !HtmlFlow::isPositioned(tag.name.c_str(), tag.attrs.begin(), tag.attrs.end()))
                ? HtmlFlow::flowBox(parent)
                : parent;
        build_element(tag, content, host, parent);

        // flex-grow on any widget that is not a layout container. Not in an HTML flow box: blocks of
        // a page have no grow, and LVGL would size it from the box's own height, which is its
        // content's (a loop: the result swings with the neighbours, see random page gen_0075)
        if (!UI::isContainer(tag.id) && !HtmlFlow::inFlow(host)) {
            const P::String grow = tag.attrs.get("grow");
            if (!grow.empty()) {
                lv_obj_t *lastChild = lv_obj_get_child(host, -1);
                if (lastChild) {
                    lv_obj_set_flex_grow(lastChild, atoi(grow.c_str()));
                }
            }
        }
    }
}

int ui_html_render_internal(const char *html) {
    if (!html) return INVALID_INDEX;
    
    LOG_D(Log::UI, "render_internal: starting");
    
    const char *p = html;
    
    HtmlFlow::reset();  // a new app: the HTML layer forgets the previous one's objects
    UI::Dom::begin();   // ...and the tree its script sees is built anew
    
    // Pass 0: Parse <head>/<app> sections
    LOG_D(Log::UI, "render_internal: parse_head...");
    parse_head(html);
    LOG_D(Log::UI, "render_internal: parse_head done");
    
    if (!g_core.app().appVersion().empty() && g_core.app().appVersion() != "0.0") {
        LOG_D(Log::UI, "========================================");
        LOG_D(Log::UI, "  App v%s (engine v%s)", g_core.app().appVersion().c_str(), Core::version());
        LOG_D(Log::UI, "========================================");
    }
    
    // Collect page content for later parsing (static to save stack)
    struct PageContent { const char *start; int len; lv_obj_t *parent; std::string_view attrs; };
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
        size_t gi = g_core.app().groupsCount();
        g_core.app().addGroup();
        
        g_core.app().group(gi).id = getAttr(astart, aend, "id");
        if (g_core.app().group(gi).id.empty()) {
            LOG_W(Log::UI, "group without id");
            g_core.app().popGroup();
            continue;
        }
        
        g_core.app().group(gi).default_page = getAttr(astart, aend, "default");
        
        g_core.app().group(gi).orientation = UI::Orientation::Horizontal;
        {
            auto orient = getAttr(astart, aend, "orientation");
            if (orient == "vertical" || orient == "v") {
                g_core.app().group(gi).orientation = UI::Orientation::Vertical;
            }
        }
        
        g_core.app().group(gi).indicator = UI::IndicatorType::Scrollbar;
        {
            auto ind = getAttr(astart, aend, "indicator");
            if (ind == "dots") {
                g_core.app().group(gi).indicator = UI::IndicatorType::Dots;
            } else if (ind == "none") {
                g_core.app().group(gi).indicator = UI::IndicatorType::None;
            }
        }
        
        // Find </group>
        const char *group_content_start = p;
        const char *gclose = findTagClose(p, Layout::Group);
        if (!gclose) {
            LOG_E(Log::UI, "No </group> for %s", g_core.app().group(gi).id.c_str());
            break;
        }
        
        g_core.app().group(gi).create(get_screen());
        
        // Parse pages inside this group
        const char *pp = group_content_start;
        while (pp < gclose && (int)g_core.app().group(gi).page_ids.size() < MAX_PAGES_PER_GROUP) {
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
                LOG_W(Log::UI, "page without id in group %s", g_core.app().group(gi).id.c_str());
                continue;
            }
            
            const char *page_content_start = pp;
            const char *pclose = findTagClose(pp, Layout::Page);
            if (!pclose || pclose > gclose) {
                LOG_E(Log::UI, "No </page> for %s in group %s", page_id.c_str(), g_core.app().group(gi).id.c_str());
                break;
            }
            
            lv_obj_t *tile = g_core.app().group(gi).addTile(page_id);
            
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
            contents[content_count].attrs = std::string_view(pastart, static_cast<size_t>(paend - pastart));
            content_count++;
            
            const P::String full_id = g_core.app().group(gi).id + "/" + page_id;
            g_core.app().addPage(full_id.c_str(), tile);
            
            LOG_D(Log::UI, "  page: %s (tile %d)", full_id.c_str(), (int)g_core.app().group(gi).page_ids.size() - 1);
            
            pp = pclose + tagCloseLen(Layout::Page);
        }
        
        g_core.app().group(gi).finalize((int)gi);
        
        // Hide if not first
        if (g_core.app().groupsCount() > 1 || g_core.app().pagesCount() > 0) {
            g_core.app().group(gi).hide();
        }
        
        g_core.app().addPage(g_core.app().group(gi).id.c_str(), g_core.app().group(gi).tileview);
        LOG_D(Log::UI, "group: %s (%d pages, %s)", g_core.app().group(gi).id.c_str(), (int)g_core.app().group(gi).page_ids.size(),
                 g_core.app().group(gi).isHorizontal() ? "horizontal" : "vertical");
        
        p = gclose + tagCloseLen(Layout::Group);
    }
    
    LOG_D(Log::UI, "render_internal: Pass 2 - standalone pages");
    // Pass 2: Find standalone <page> tags (not inside groups)
    p = html;
    int pass2_iterations = 0;
    while (*p && g_core.app().pagesCount() < MAX_SCREENS) {
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
        if (g_core.app().pagesCount() > 0 || g_core.app().groupsCount() > 0) {
            lv_obj_add_flag(scr, LV_OBJ_FLAG_HIDDEN);
        }
        
        g_core.app().pushPage(id.c_str(), scr);

        contents[content_count].start = content_start;
        contents[content_count].len = pclose - content_start;
        contents[content_count].parent = scr;
        contents[content_count].attrs = std::string_view(astart, static_cast<size_t>(aend - astart));
        content_count++;

        g_core.app().addPage(id.c_str(), scr);
        p = pclose + tagCloseLen(Layout::Page);
    }
    
    LOG_I(Log::UI, "render_internal: Pass 3 - children (%d pages)", content_count);
    // Pass 3: Parse children for all pages
    for (int i = 0; i < content_count; i++) {
        LOG_I(Log::UI, "  page[%d]: len=%d parent=%p", i, contents[i].len, contents[i].parent);
        UI::Dom::element(contents[i].parent, "page", contents[i].attrs);
        {
            UI::CssScope page("page", contents[i].attrs,  // #settings button {} sees its page
                              std::string_view(contents[i].start, static_cast<size_t>(contents[i].len)));
            parse_children(contents[i].start, contents[i].len, contents[i].parent);
        }
        apply_canvas(contents[i].parent, contents[i].attrs);
    }
    UI::Dom::end();
    
    LOG_I(Log::UI, "Done: %d g_core.app().elements, %d groups, %d standalone pages", 
             (int)g_core.app().elements.size(), g_core.app().groupsCount(), g_core.app().pagesCount());
    
    // Apply <ui default="/page"> — navigate to specified initial page
    if (!g_core.app().defaultPage().empty()) {
        LOG_I(Log::UI, "Navigating to default page: %s", g_core.app().defaultPage().c_str());
        g_core.app().applyZIndex();
        ui_show_page_internal(g_core.app().defaultPage().c_str());
    }
    
    // Memory after UI render
    Serial.print("[Heap] After render: DRAM=");
    Serial.print(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Serial.print(" PSRAM=");
    Serial.println(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    
    return (int)g_core.app().elements.size();
}


