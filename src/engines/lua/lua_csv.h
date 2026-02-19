#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_csv.h - CSV namespace for Lua
 *   CSV.load(filename)       — load from file
 *   CSV.loadText(text)       — parse from string
 *   csv:records(count?)      — get records as dicts
 *   csv:rows(count?)         — get records as arrays
 *   csv:rawText()            — serialize to string
 *   csv:add(record)          — add record (dict or array)
 *   csv:save(onlyNew?)       — save to file
 */

namespace LuaCSV {

void registerAll(lua_State* L);

} // namespace LuaCSV
