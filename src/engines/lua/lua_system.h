#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_system.h
 *   app.exit([code[, msg]])  + alias exit()
 *   app.launch(name)
 *   canvas.* (unchanged)
 */

namespace LuaSystem {

using LaunchCallback = void(*)(const char* name);
using HomeCallback = void(*)();

void setLaunchCallback(LaunchCallback cb);
void setHomeCallback(HomeCallback cb);

void registerAll(lua_State* L);

} // namespace LuaSystem
