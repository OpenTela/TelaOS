#include "engines/lua/lua_timer.h"
#include "core/call_queue.h"
#include "utils/log_config.h"
#include "lvgl.h"

namespace LuaTimer {

static const char* TAG = "LuaTimer";
static lua_State* g_luaState = nullptr;

// Timer callback data — supports both string name and function ref
struct TimerData {
    P::String funcName;    // non-empty if callback is a string
    int funcRef = LUA_NOREF;  // valid if callback is a function
    bool oneshot = true;
};

static void timer_callback(lv_timer_t* timer) {
    TimerData* data = (TimerData*)lv_timer_get_user_data(timer);
    if (!data) return;
    
    if (data->funcRef != LUA_NOREF && g_luaState) {
        // Function ref callback — call directly
        lua_rawgeti(g_luaState, LUA_REGISTRYINDEX, data->funcRef);
        if (lua_pcall(g_luaState, 0, 0, 0) != LUA_OK) {
            LOG_E(Log::LUA, "timer callback error: %s", lua_tostring(g_luaState, -1));
            lua_pop(g_luaState, 1);
        }
    } else if (!data->funcName.empty()) {
        // String name callback — push to queue
        CallQueue::push(data->funcName);
    }
    
    if (data->oneshot) {
        if (data->funcRef != LUA_NOREF && g_luaState) {
            luaL_unref(g_luaState, LUA_REGISTRYINDEX, data->funcRef);
        }
        delete data;
        lv_timer_delete(timer);
    }
}

// Helper: extract callback from arg 1 (string or function)
static TimerData* extractCallback(lua_State* L) {
    TimerData* data = new TimerData();
    
    if (lua_isfunction(L, 1)) {
        lua_pushvalue(L, 1);
        data->funcRef = luaL_ref(L, LUA_REGISTRYINDEX);
    } else if (lua_isstring(L, 1)) {
        data->funcName = lua_tostring(L, 1);
    } else {
        delete data;
        luaL_error(L, "timer: expected function or string, got %s", luaL_typename(L, 1));
        return nullptr;
    }
    
    return data;
}

// timer.once(callback, delayMs)
static int lua_timer_once(lua_State* L) {
    TimerData* data = extractCallback(L);
    if (!data) return 0;
    
    int delayMs = (int)luaL_checkinteger(L, 2);
    if (delayMs < 1) delayMs = 1;
    
    data->oneshot = true;
    
    LOG_D(Log::LUA, "timer.once(%s, %d)", 
          data->funcName.empty() ? "<function>" : data->funcName.c_str(), delayMs);
    
    lv_timer_t* timer = lv_timer_create(timer_callback, delayMs, data);
    lv_timer_set_repeat_count(timer, 1);
    
    return 0;
}

// timer.interval(callback, intervalMs)
static int lua_timer_interval(lua_State* L) {
    TimerData* data = extractCallback(L);
    if (!data) return 0;
    
    int intervalMs = (int)luaL_checkinteger(L, 2);
    if (intervalMs < 1) intervalMs = 1;
    
    data->oneshot = false;
    
    LOG_D(Log::LUA, "timer.interval(%s, %d)", 
          data->funcName.empty() ? "<function>" : data->funcName.c_str(), intervalMs);
    
    lv_timer_create(timer_callback, intervalMs, data);
    
    return 0;
}

// timer.clear(name) — clear all timers with matching function name
static int lua_timer_clear(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    LOG_D(Log::LUA, "timer.clear('%s')", name);
    
    // Walk LVGL timer list and remove matching
    lv_timer_t* t = lv_timer_get_next(nullptr);
    while (t) {
        lv_timer_t* next = lv_timer_get_next(t);
        TimerData* data = (TimerData*)lv_timer_get_user_data(t);
        if (data && data->funcName == name) {
            if (data->funcRef != LUA_NOREF && g_luaState) {
                luaL_unref(g_luaState, LUA_REGISTRYINDEX, data->funcRef);
            }
            delete data;
            lv_timer_delete(t);
        }
        t = next;
    }
    
    return 0;
}

static int lua_getQueueStats(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)CallQueue::size());
    lua_pushinteger(L, (lua_Integer)CallQueue::droppedCount());
    return 2;
}

static const luaL_Reg timer_lib[] = {
    {"once",     lua_timer_once},
    {"interval", lua_timer_interval},
    {"clear",    lua_timer_clear},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    g_luaState = L;
    
    // timer.* namespace
    luaL_newlib(L, timer_lib);
    lua_setglobal(L, "timer");
    
    // Alias: setTimeout() for backward compat
    lua_register(L, "setTimeout", lua_timer_once);
    
    // Internal: getQueueStats (not part of public API)
    lua_register(L, "getQueueStats", lua_getQueueStats);
    
    LOG_I(Log::LUA, "Registered: timer.once/interval/clear + setTimeout()");
}

} // namespace LuaTimer
