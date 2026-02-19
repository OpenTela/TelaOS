#include "engines/lua/lua_engine.h"
#include "engines/lua/lua_system.h"
#include "engines/lua/lua_timer.h"
#include "engines/lua/lua_fetch.h"
#include "engines/lua/lua_ui.h"
#include "engines/lua/lua_csv.h"
#include "engines/lua/lua_yaml.h"
#include "utils/log_config.h"
#include "esp_heap_caps.h"
#include <cstring>

LuaEngine* LuaEngine::s_instance = nullptr;

static const char* TAG = "LuaEngine";

// PSRAM allocator for Lua
static void* lua_psram_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    (void)ud; (void)osize;
    
    if (nsize == 0) {
        free(ptr);
        return nullptr;
    }
    
    if (ptr == nullptr) {
        void* p = heap_caps_malloc(nsize, MALLOC_CAP_SPIRAM);
        if (!p) p = malloc(nsize);
        return p;
    }
    
    void* p = heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM);
    if (!p) p = realloc(ptr, nsize);
    return p;
}

bool LuaEngine::init() {
    LOG_D(Log::LUA, "========================================");
    LOG_D(Log::LUA, "LuaEngine::init() v%s - Lua 5.4", VERSION);
    LOG_D(Log::LUA, "========================================");
    
    s_instance = this;
    
    m_lua = lua_newstate(lua_psram_alloc, nullptr);
    if (!m_lua) {
        LOG_E(Log::LUA, "Failed to create Lua state");
        return false;
    }
    
    LOG_I(Log::LUA, "Lua VM allocated in PSRAM");
    
    luaL_openlibs(m_lua);
    createStateTable();
    createConfigTable();
    
    LuaSystem::registerAll(m_lua);
    LuaTimer::registerAll(m_lua);
    LuaFetch::registerAll(m_lua);
    LuaUI::registerAll(m_lua);
    LuaCSV::registerAll(m_lua);
    LuaYAML::registerAll(m_lua);
    
    LOG_I(Log::LUA, "Lua initialized successfully");
    return true;
}

/**
 * Preprocess Lua source: auto-generate forward declarations for local functions.
 * 
 * Problem: In Lua, `local function foo()` is only visible AFTER its declaration.
 *          If bar() calls foo() but foo() is defined below, it fails at runtime.
 * 
 * Solution (Arduino-style):
 *   1. Find all `local function NAME` in the source
 *   2. Prepend `local NAME1, NAME2, ...` at the top
 *   3. Replace `local function NAME(` → `NAME = function(`
 *      (so it assigns to the forward-declared local instead of creating a new one)
 * 
 * Safe: only transforms `local function`. Regular `function` and nested code untouched.
 */
static P::String preprocessForwardDecl(const char* source) {
    // First pass: collect all "local function NAME" 
    P::Array<P::String> names;
    
    const char* p = source;
    while (*p) {
        // Skip whitespace at start of line
        const char* lineStart = p;
        while (*p == ' ' || *p == '\t') p++;
        
        // Skip comment lines
        if (*p == '-' && *(p + 1) == '-') {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        
        if (strncmp(p, "local function ", 15) == 0) {
            const char* nameStart = p + 15;
            const char* nameEnd = nameStart;
            while (*nameEnd && *nameEnd != '(' && *nameEnd != ' ' 
                   && *nameEnd != '\r' && *nameEnd != '\n') {
                nameEnd++;
            }
            if (nameEnd > nameStart) {
                names.emplace_back(nameStart, nameEnd - nameStart);
            }
        }
        
        // Skip to next line
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    
    if (names.empty()) return P::String(source);
    
    // Second pass: build modified source
    P::String body;
    body.reserve(strlen(source) + 64);
    
    p = source;
    while (*p) {
        const char* lineStart = p;
        
        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        const char* trimPos = p;
        
        // Check for "local function NAME"
        bool replaced = false;
        if (strncmp(p, "local function ", 15) == 0) {
            const char* nameStart = p + 15;
            const char* nameEnd = nameStart;
            while (*nameEnd && *nameEnd != '(' && *nameEnd != ' '
                   && *nameEnd != '\r' && *nameEnd != '\n') {
                nameEnd++;
            }
            P::String name(nameStart, nameEnd - nameStart);
            
            for (const auto& n : names) {
                if (n == name) {
                    // Keep leading whitespace
                    body.append(lineStart, trimPos - lineStart);
                    // Replace: NAME = function(...)
                    body += name;
                    body += " = function";
                    // Copy rest of line from after NAME
                    p = nameEnd;
                    while (*p && *p != '\n') {
                        body += *p;
                        p++;
                    }
                    if (*p == '\n') {
                        body += '\n';
                        p++;
                    }
                    replaced = true;
                    break;
                }
            }
        }
        
        if (!replaced) {
            // Copy line as-is
            p = lineStart;
            while (*p && *p != '\n') {
                body += *p;
                p++;
            }
            if (*p == '\n') {
                body += '\n';
                p++;
            }
        }
    }
    
    // Build header: local name1, name2, name3
    P::String header = "local ";
    for (size_t i = 0; i < names.size(); i++) {
        if (i > 0) header += ", ";
        header += names[i];
    }
    header += "\n";
    
    LOG_D(Log::LUA, "Forward decl: %s", header.c_str());
    
    return header + body;
}

bool LuaEngine::execute(const char* code) {
    if (!m_lua) return false;
    
    P::String processed = preprocessForwardDecl(code);
    
    LOG_D(Log::LUA, "execute() - %d bytes", (int)processed.size());
    
    int result = luaL_dostring(m_lua, processed.c_str());
    if (result != LUA_OK) {
        const char* err = lua_tostring(m_lua, -1);
        LOG_E(Log::LUA, "Lua error: %s", err ? err : "unknown");
        lua_pop(m_lua, 1);
        return false;
    }
    
    return true;
}

bool LuaEngine::call(const char* func) {
    if (!m_lua) return false;
    
    lua_getglobal(m_lua, func);
    if (!lua_isfunction(m_lua, -1)) {
        LOG_W(Log::LUA, "Function '%s' not found", func);
        lua_pop(m_lua, 1);
        return false;
    }
    
    int result = lua_pcall(m_lua, 0, 0, 0);
    if (result != LUA_OK) {
        const char* err = lua_tostring(m_lua, -1);
        LOG_E(Log::LUA, "Lua call error: %s", err ? err : "unknown");
        lua_pop(m_lua, 1);
        return false;
    }
    
    return true;
}

const char* LuaEngine::name() const {
    return "LuaEngine";
}

void LuaEngine::shutdown() {
    LOG_I(Log::LUA, "shutdown()");
    
    if (m_lua) {
        lua_close(m_lua);
        m_lua = nullptr;
    }
    
    s_instance = nullptr;
}

// ============ Config table ============

void LuaEngine::createConfigTable() {
    lua_newtable(m_lua);
    lua_setglobal(m_lua, "config");
}

void LuaEngine::setConfig(const char* key, const char* value) {
    BaseScriptEngine::setConfig(key, value);
    
    if (m_lua) {
        lua_getglobal(m_lua, "config");
        lua_pushstring(m_lua, value);
        lua_setfield(m_lua, -2, key);
        lua_pop(m_lua, 1);
    }
}

// ============ State overrides: base + Lua table sync ============

void LuaEngine::syncLuaState(const char* key) {
    if (m_lua) {
        lua_getglobal(m_lua, "_state_data");
        pushTypedValue(m_lua, key);
        lua_setfield(m_lua, -2, key);
        lua_pop(m_lua, 1);
    }
}

void LuaEngine::setState(const char* key, const char* value) {
    // Skip if unchanged
    P::String oldVal = State::store().getAsString(key);
    if (oldVal == value) return;
    
    BaseScriptEngine::setState(key, value);
    syncLuaState(key);
}

void LuaEngine::setState(const char* key, int value) {
    BaseScriptEngine::setState(key, value);
    syncLuaState(key);
}

void LuaEngine::setState(const char* key, bool value) {
    BaseScriptEngine::setState(key, value);
    syncLuaState(key);
}

void LuaEngine::setStateSilent(const char* key, const char* value) {
    BaseScriptEngine::setStateSilent(key, value);
    syncLuaState(key);
}

// ============ Lua internals ============

void LuaEngine::pushTypedValue(lua_State* L, const char* key) {
    auto& store = State::store();
    VarType type = store.getType(key);
    
    switch (type) {
        case VarType::Bool:
            lua_pushboolean(L, store.getBool(key) ? 1 : 0);
            break;
        case VarType::Int:
            lua_pushinteger(L, store.getInt(key));
            break;
        case VarType::Float:
            lua_pushnumber(L, store.getFloat(key));
            break;
        default:
            lua_pushstring(L, store.getString(key).c_str());
            break;
    }
}

void LuaEngine::createStateTable() {
    lua_newtable(m_lua);
    lua_setglobal(m_lua, "_state_data");
    
    lua_newtable(m_lua);
    lua_newtable(m_lua);
    
    lua_pushcfunction(m_lua, lua_state_index);
    lua_setfield(m_lua, -2, "__index");
    
    lua_pushcfunction(m_lua, lua_state_newindex);
    lua_setfield(m_lua, -2, "__newindex");
    
    lua_setmetatable(m_lua, -2);
    lua_setglobal(m_lua, "state");
}

int LuaEngine::lua_state_index(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    
    auto& store = State::store();
    VarType type = store.getType(key);
    
    switch (type) {
        case VarType::Bool:
            lua_pushboolean(L, store.getBool(key) ? 1 : 0);
            break;
        case VarType::Int:
            lua_pushinteger(L, store.getInt(key));
            break;
        case VarType::Float:
            lua_pushnumber(L, store.getFloat(key));
            break;
        default:
            lua_pushstring(L, store.getString(key).c_str());
            break;
    }
    
    return 1;
}

int LuaEngine::lua_state_newindex(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    
    P::String valueStr;
    int vtype = lua_type(L, 3);
    
    switch (vtype) {
        case LUA_TBOOLEAN:
            valueStr = lua_toboolean(L, 3) ? "true" : "false";
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(L, 3)) {
                valueStr = std::to_string(lua_tointeger(L, 3));
            } else {
                valueStr = std::to_string(lua_tonumber(L, 3));
            }
            break;
        case LUA_TSTRING:
            valueStr = lua_tostring(L, 3);
            break;
        default:
            valueStr = "";
            break;
    }
    
    LOG_V(Log::LUA, "state.%s = '%s' (type=%d)", key, valueStr.c_str(), vtype);
    
    // Update Lua _state_data table
    lua_getglobal(L, "_state_data");
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
    
    // Update StateStore + notify UI
    if (s_instance) {
        auto& store = State::store();
        VarType type = store.getType(key);
        
        switch (vtype) {
            case LUA_TBOOLEAN:
                store.setBool(key, lua_toboolean(L, 3), false);
                break;
            case LUA_TNUMBER:
                if (type == VarType::Float || !lua_isinteger(L, 3)) {
                    store.setFloat(key, static_cast<float>(lua_tonumber(L, 3)), false);
                } else {
                    store.setInt(key, static_cast<int>(lua_tointeger(L, 3)), false);
                }
                break;
            default:
                store.setString(key, valueStr, false);
                break;
        }
        
        if (s_instance->m_stateCallback) {
            s_instance->m_stateCallback(key, valueStr.c_str());
        }
    }
    
    return 0;
}
