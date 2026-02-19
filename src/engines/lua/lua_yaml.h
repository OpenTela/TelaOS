/**
 * lua_yaml.h - YAML API for Lua
 *
 * YAML.load(filename)    → yaml object (from file)
 * YAML.loadText(text)    → yaml object (from string)
 * yaml:get("a.b.c")     → value
 * yaml:set("a.b.c", v)  → true
 * yaml:tree()            → lua table (reference, not copy)
 * yaml:save(filename?)   → true
 * yaml:toText()          → string
 */
#pragma once

extern "C" {
#include "lua.h"
}

namespace LuaYAML {
    void registerAll(lua_State* L);
}
