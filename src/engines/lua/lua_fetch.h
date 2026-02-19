#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_fetch.h - net.* namespace
 *   net.fetch(opts, cb)  + alias fetch()
 *   net.connected()
 */

namespace LuaFetch {

void registerAll(lua_State* L);

} // namespace LuaFetch
