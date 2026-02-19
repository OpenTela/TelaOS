#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_timer.h - timer.* namespace
 *   timer.once(callback, ms)      — one-shot timer
 *   timer.interval(callback, ms)  — repeating timer
 *   timer.clear(name)             — cancel by name
 *
 * callback: string "funcName" or function reference
 * Alias: setTimeout() = timer.once()
 */
namespace LuaTimer {

/// Register timer.*, setTimeout(), getQueueStats() into Lua
void registerAll(lua_State* L);

} // namespace LuaTimer
