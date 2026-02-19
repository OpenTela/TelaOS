#pragma once

#include "core/base_script_engine.h"
#include "core/state_store.h"

extern "C" {
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
}

/**
 * LuaEngine - Lua 5.4 script engine.
 * 
 * Extends BaseScriptEngine with Lua VM table synchronization.
 * THREAD SAFETY: All calls must come from main loop via CallQueue::process().
 */
class LuaEngine : public BaseScriptEngine {
private:
    static constexpr const char* TAG = "LuaEngine";
    static constexpr const char* VERSION = "1.5.0";
    
    lua_State* m_lua = nullptr;
    static LuaEngine* s_instance;

public:
    bool init() override;
    bool execute(const char* code) override;
    bool call(const char* func) override;
    const char* name() const override;
    void shutdown() override;
    
    // Overrides: sync Lua VM tables after base setState
    void setState(const char* key, const char* value) override;
    void setState(const char* key, int value) override;
    void setState(const char* key, bool value) override;
    void setStateSilent(const char* key, const char* value) override;
    void setConfig(const char* key, const char* value) override;
    
    lua_State* getLuaState() { return m_lua; }

private:
    void createStateTable();
    void createConfigTable();
    void pushTypedValue(lua_State* L, const char* key);
    void syncLuaState(const char* key);
    
    static int lua_state_index(lua_State* L);
    static int lua_state_newindex(lua_State* L);
};
