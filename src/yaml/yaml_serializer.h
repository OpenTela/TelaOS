/**
 * yaml_serializer.h - Serialize Lua table to YAML text
 *
 * Reads Lua table from stack and produces formatted YAML string.
 * Handles: maps, arrays, nested structures, type preservation.
 */
#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace YamlSerializer {

// Check if a Lua table is an array (sequential integer keys from 1)
inline bool isArray(lua_State* L, int index) {
    if (!lua_istable(L, index)) return false;

    int absIdx = (index > 0) ? index : lua_gettop(L) + index + 1;
    int maxKey = 0;
    int count = 0;

    lua_pushnil(L);
    while (lua_next(L, absIdx)) {
        lua_pop(L, 1); // pop value
        if (lua_type(L, -1) != LUA_TNUMBER) return false;
        int key = (int)lua_tonumber(L, -1);
        if (key <= 0) return false;
        if (key > maxKey) maxKey = key;
        count++;
    }

    return count > 0 && count == maxKey;
}

// Check if string needs quoting in YAML
inline bool needsQuoting(const std::string& s) {
    if (s.empty()) return true;
    // Strings that look like booleans or numbers
    if (s == "true" || s == "false" || s == "yes" || s == "no" ||
        s == "null" || s == "~") return true;

    // Check if it parses as a number
    char* end = nullptr;
    strtod(s.c_str(), &end);
    if (end && end != s.c_str() && *end == '\0') return true;

    // Contains special chars
    for (char c : s) {
        if (c == ':' || c == '#' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == ',' || c == '&' ||
            c == '*' || c == '!' || c == '|' || c == '>' ||
            c == '\'' || c == '"' || c == '%' || c == '@') return true;
    }

    // Starts with special
    if (s[0] == '-' || s[0] == '?' || s[0] == ' ') return true;

    return false;
}

inline std::string quoteString(const std::string& s) {
    // Use single quotes for simplicity
    // Escape single quotes by doubling them
    std::string result = "'";
    for (char c : s) {
        if (c == '\'') result += "''";
        else result += c;
    }
    result += "'";
    return result;
}

inline std::string formatValue(lua_State* L, int index) {
    int type = lua_type(L, index);
    switch (type) {
        case LUA_TNUMBER: {
            double num = lua_tonumber(L, index);
            if (num == (int)num && std::abs(num) < 1e15) {
                return std::to_string((int)num);
            }
            char buf[64];
            snprintf(buf, sizeof(buf), "%.6g", num);
            return buf;
        }
        case LUA_TBOOLEAN:
            return lua_toboolean(L, index) ? "true" : "false";
        case LUA_TSTRING: {
            std::string s = lua_tostring(L, index);
            if (needsQuoting(s)) return quoteString(s);
            return s;
        }
        case LUA_TNIL:
            return "null";
        default:
            return "null";
    }
}

// Forward declaration
inline void serializeTable(lua_State* L, int index, std::string& out, int indent);

inline std::string indentStr(int level) {
    return std::string(level * 2, ' ');
}

inline void serializeArray(lua_State* L, int index, std::string& out, int indent) {
    int absIdx = (index > 0) ? index : lua_gettop(L) + index + 1;
    int len = 0;

    // Get array length
    lua_pushnil(L);
    while (lua_next(L, absIdx)) {
        lua_pop(L, 1);
        len++;
    }

    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, absIdx, i);
        int type = lua_type(L, -1);

        if (type == LUA_TTABLE) {
            if (isArray(L, -1)) {
                // Nested array — rare in YAML configs, serialize inline
                out += indentStr(indent) + "- ";
                // Inline: [a, b, c]
                out += "[";
                int subLen = 0;
                lua_pushnil(L);
                while (lua_next(L, -2)) { lua_pop(L, 1); subLen++; }
                for (int j = 1; j <= subLen; j++) {
                    if (j > 1) out += ", ";
                    lua_rawgeti(L, -1, j);
                    out += formatValue(L, -1);
                    lua_pop(L, 1);
                }
                out += "]\n";
            } else {
                // Map inside array
                out += indentStr(indent) + "-\n";
                serializeTable(L, -1, out, indent + 1);
            }
        } else {
            out += indentStr(indent) + "- " + formatValue(L, -1) + "\n";
        }
        lua_pop(L, 1);
    }
}

inline void serializeTable(lua_State* L, int index, std::string& out, int indent) {
    int absIdx = (index > 0) ? index : lua_gettop(L) + index + 1;

    if (isArray(L, absIdx)) {
        serializeArray(L, absIdx, out, indent);
        return;
    }

    // Collect keys for deterministic order
    std::vector<std::string> keys;
    lua_pushnil(L);
    while (lua_next(L, absIdx)) {
        lua_pop(L, 1); // pop value
        if (lua_type(L, -1) == LUA_TSTRING) {
            keys.push_back(lua_tostring(L, -1));
        }
    }

    // Sort keys alphabetically for deterministic output
    std::sort(keys.begin(), keys.end());

    for (const auto& key : keys) {
        lua_pushlstring(L, key.c_str(), key.size());
        lua_gettable(L, absIdx);

        int type = lua_type(L, -1);
        if (type == LUA_TTABLE) {
            out += indentStr(indent) + key + ":\n";
            serializeTable(L, -1, out, indent + 1);
        } else {
            out += indentStr(indent) + key + ": " + formatValue(L, -1) + "\n";
        }
        lua_pop(L, 1);
    }
}

// Serialize Lua table at given stack index to YAML string
inline std::string serialize(lua_State* L, int index) {
    std::string out;
    if (lua_istable(L, index)) {
        serializeTable(L, index, out, 0);
    }
    return out;
}

} // namespace YamlSerializer
