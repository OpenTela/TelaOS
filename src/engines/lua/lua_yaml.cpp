/**
 * lua_yaml.cpp - YAML API for Lua
 *
 * Data is stored as a Lua table (registry ref).
 * tree() returns that same table — not a copy.
 * get/set navigate the table by dot-separated path.
 * save/toText serialize the table to YAML text.
 */
#include "engines/lua/lua_yaml.h"
#include "yaml/yaml_parser.h"
#include "yaml/yaml_serializer.h"
#include <LittleFS.h>
#include <string>
#include <cstring>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

// ─── Userdata ───────────────────────────────────────────

struct YamlObject {
    int tableRef;           // LUA_REGISTRYINDEX ref to data table
    std::string filename;   // for save() — empty if loadText

    YamlObject() : tableRef(LUA_NOREF) {}
    ~YamlObject() {} // ref freed in __gc
};

static const char* YAML_MT = "YAML";

static YamlObject* checkYaml(lua_State* L, int idx) {
    return static_cast<YamlObject*>(luaL_checkudata(L, idx, YAML_MT));
}

// ─── Path navigation helpers ────────────────────────────

// Split "a.b.c" into segments, push table navigation.
// Leaves the parent table and last key on stack for set operations.
// For get: navigate fully and push the final value.

// Navigate to parent of path, pushing the last key name.
// Stack: [..., parentTable, lastKeyString]
// Returns true if parent exists.
static bool navigateToParent(lua_State* L, int tableIdx, const char* path, bool createMissing) {
    // Push the root table
    lua_pushvalue(L, tableIdx);

    const char* start = path;
    const char* dot = nullptr;

    while ((dot = strchr(start, '.')) != nullptr) {
        // Extract segment
        std::string segment(start, dot - start);
        start = dot + 1;

        lua_pushlstring(L, segment.c_str(), segment.size());
        lua_gettable(L, -2);

        if (lua_isnil(L, -1)) {
            if (createMissing) {
                lua_pop(L, 1); // pop nil
                lua_newtable(L);
                // Set in parent
                lua_pushlstring(L, segment.c_str(), segment.size());
                lua_pushvalue(L, -2); // copy new table
                lua_settable(L, -4); // parent[segment] = newTable
                // Now new table is on top, remove parent
                lua_remove(L, -2);
            } else {
                lua_pop(L, 2); // pop nil and table
                return false;
            }
        } else if (!lua_istable(L, -1)) {
            if (createMissing) {
                // Overwrite non-table with new table
                lua_pop(L, 1);
                lua_newtable(L);
                lua_pushlstring(L, segment.c_str(), segment.size());
                lua_pushvalue(L, -2);
                lua_settable(L, -4);
                lua_remove(L, -2);
            } else {
                lua_pop(L, 2);
                return false;
            }
        } else {
            // It's a table, continue. Remove parent from under.
            lua_remove(L, -2);
        }
    }

    // Push last key
    lua_pushstring(L, start);
    return true;
}

// ─── YAML.load(filename) ───────────────────────────────

static int yaml_load(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);

    File file = LittleFS.open(filename, "r");
    if (!file) {
        lua_pushnil(L);
        lua_pushfstring(L, "File not found: %s", filename);
        return 2;
    }

    size_t sz = file.size();
    std::string text(sz, '\0');
    file.readBytes(&text[0], sz);
    file.close();

    // Parse YAML → Lua table
    int ok = YamlParser::parseToLua(L, text.c_str());
    if (!ok) {
        return 2; // nil, error already on stack
    }

    // Table is on top of stack. Store as ref.
    int tableRef = luaL_ref(L, LUA_REGISTRYINDEX);

    // Create userdata
    YamlObject* yaml = static_cast<YamlObject*>(lua_newuserdata(L, sizeof(YamlObject)));
    new (yaml) YamlObject();
    yaml->tableRef = tableRef;
    yaml->filename = filename;

    luaL_getmetatable(L, YAML_MT);
    lua_setmetatable(L, -2);
    return 1;
}

// ─── YAML.loadText(text) ───────────────────────────────

static int yaml_loadText(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);

    int ok = YamlParser::parseToLua(L, text);
    if (!ok) {
        return 2;
    }

    int tableRef = luaL_ref(L, LUA_REGISTRYINDEX);

    YamlObject* yaml = static_cast<YamlObject*>(lua_newuserdata(L, sizeof(YamlObject)));
    new (yaml) YamlObject();
    yaml->tableRef = tableRef;

    luaL_getmetatable(L, YAML_MT);
    lua_setmetatable(L, -2);
    return 1;
}

// ─── yaml:get(path) ────────────────────────────────────

static int yaml_get(lua_State* L) {
    YamlObject* yaml = checkYaml(L, 1);
    const char* path = luaL_checkstring(L, 2);

    lua_rawgeti(L, LUA_REGISTRYINDEX, yaml->tableRef);
    int tableIdx = lua_gettop(L);

    // Simple key (no dots)?
    if (!strchr(path, '.')) {
        lua_pushstring(L, path);
        lua_gettable(L, tableIdx);
        return 1;
    }

    // Navigate dot path
    if (navigateToParent(L, tableIdx, path, false)) {
        // Stack: parentTable, lastKey
        lua_gettable(L, -2);
        return 1;
    }

    lua_pushnil(L);
    return 1;
}

// ─── yaml:set(path, value) ─────────────────────────────

static int yaml_set(lua_State* L) {
    YamlObject* yaml = checkYaml(L, 1);
    const char* path = luaL_checkstring(L, 2);
    // value is at index 3

    lua_rawgeti(L, LUA_REGISTRYINDEX, yaml->tableRef);
    int tableIdx = lua_gettop(L);

    if (!strchr(path, '.')) {
        // Simple key
        lua_pushstring(L, path);
        lua_pushvalue(L, 3);
        lua_settable(L, tableIdx);
        lua_pushboolean(L, 1);
        return 1;
    }

    // Navigate, creating missing tables
    if (navigateToParent(L, tableIdx, path, true)) {
        // Stack: ..., parentTable, lastKey
        lua_pushvalue(L, 3); // value
        lua_settable(L, -3); // parent[lastKey] = value
        lua_pushboolean(L, 1);
        return 1;
    }

    lua_pushboolean(L, 0);
    lua_pushstring(L, "Cannot create path");
    return 2;
}

// ─── yaml:tree() ───────────────────────────────────────

static int yaml_tree(lua_State* L) {
    YamlObject* yaml = checkYaml(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, yaml->tableRef);
    return 1;
}

// ─── yaml:save(filename?) ──────────────────────────────

static int yaml_save(lua_State* L) {
    YamlObject* yaml = checkYaml(L, 1);
    const char* filename = nullptr;

    if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
        filename = lua_tostring(L, 2);
    } else {
        filename = yaml->filename.c_str();
    }

    if (!filename || !filename[0]) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "No filename (use YAML.load or pass filename)");
        return 2;
    }

    // Get the table
    lua_rawgeti(L, LUA_REGISTRYINDEX, yaml->tableRef);
    std::string text = YamlSerializer::serialize(L, -1);
    lua_pop(L, 1);

    // Write to file
    File file = LittleFS.open(filename, "w");
    if (!file) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "Cannot write: %s", filename);
        return 2;
    }

    file.write((const uint8_t*)text.c_str(), text.size());
    file.close();

    // Update filename if saving to new path
    if (lua_gettop(L) >= 2 && lua_isstring(L, 2)) {
        yaml->filename = lua_tostring(L, 2);
    }

    lua_pushboolean(L, 1);
    return 1;
}

// ─── yaml:toText() ─────────────────────────────────────

static int yaml_toText(lua_State* L) {
    YamlObject* yaml = checkYaml(L, 1);

    lua_rawgeti(L, LUA_REGISTRYINDEX, yaml->tableRef);
    std::string text = YamlSerializer::serialize(L, -1);
    lua_pop(L, 1);

    lua_pushlstring(L, text.c_str(), text.size());
    return 1;
}

// ─── __gc ──────────────────────────────────────────────

static int yaml_gc(lua_State* L) {
    YamlObject* yaml = static_cast<YamlObject*>(lua_touserdata(L, 1));
    if (yaml && yaml->tableRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, yaml->tableRef);
        yaml->tableRef = LUA_NOREF;
    }
    yaml->~YamlObject();
    return 0;
}

// ─── Registration ──────────────────────────────────────

static const luaL_Reg yaml_methods[] = {
    {"get", yaml_get},
    {"set", yaml_set},
    {"tree", yaml_tree},
    {"save", yaml_save},
    {"toText", yaml_toText},
    {"__gc", yaml_gc},
    {nullptr, nullptr}
};

static const luaL_Reg yaml_functions[] = {
    {"load", yaml_load},
    {"loadText", yaml_loadText},
    {nullptr, nullptr}
};

void LuaYAML::registerAll(lua_State* L) {
    // Create metatable
    luaL_newmetatable(L, YAML_MT);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index"); // mt.__index = mt

    for (const luaL_Reg* r = yaml_methods; r->name; r++) {
        lua_pushcfunction(L, r->func);
        lua_setfield(L, -2, r->name);
    }
    lua_pop(L, 1);

    // Register YAML global table
    lua_newtable(L);
    for (const luaL_Reg* r = yaml_functions; r->name; r++) {
        lua_pushcfunction(L, r->func);
        lua_setfield(L, -2, r->name);
    }
    lua_setglobal(L, "YAML");
}
