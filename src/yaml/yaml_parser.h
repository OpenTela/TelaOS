/**
 * yaml_parser.h - Parse YAML text into Lua tables
 *
 * Supports:
 *   - Nested maps (indentation-based)
 *   - Arrays (- item)
 *   - Types: number, boolean, string, null
 *   - Comments (#)
 *   - Quoted strings ("..." and '...')
 *   - Inline arrays [a, b, c]
 *
 * Pushes result as Lua table onto the stack.
 */
#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <cstring>

namespace YamlParser {

// ─── Helpers ────────────────────────────────────────────

inline std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t')) start++;
    while (end > start && (s[end-1] == ' ' || s[end-1] == '\t')) end--;
    return s.substr(start, end - start);
}

inline std::string stripComment(const std::string& s) {
    bool inSingle = false, inDouble = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\'' && !inDouble) inSingle = !inSingle;
        else if (s[i] == '"' && !inSingle) inDouble = !inDouble;
        else if (s[i] == '#' && !inSingle && !inDouble) {
            // Must be preceded by space or be at start
            if (i == 0 || s[i-1] == ' ' || s[i-1] == '\t') {
                return trim(s.substr(0, i));
            }
        }
    }
    return s;
}

inline int measureIndent(const std::string& line) {
    int n = 0;
    for (char c : line) {
        if (c == ' ') n++;
        else if (c == '\t') n += 2;
        else break;
    }
    return n;
}

inline std::string unquote(const std::string& s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') ||
            (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

inline bool isQuoted(const std::string& s) {
    return s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\''));
}

// Push a YAML value onto Lua stack with auto-typing
inline void pushValue(lua_State* L, const std::string& val) {
    if (val.empty() || val == "null" || val == "~") {
        lua_pushnil(L);
        return;
    }

    // Quoted → always string
    if (isQuoted(val)) {
        std::string s = unquote(val);
        lua_pushlstring(L, s.c_str(), s.size());
        return;
    }

    // Boolean
    if (val == "true" || val == "yes") { lua_pushboolean(L, 1); return; }
    if (val == "false" || val == "no") { lua_pushboolean(L, 0); return; }

    // Number — try integer first, then float
    char* end = nullptr;
    double num = strtod(val.c_str(), &end);
    if (end && end != val.c_str() && *end == '\0') {
        // Check if it's an integer (no dot, no 'e')
        if (val.find('.') == std::string::npos &&
            val.find('e') == std::string::npos &&
            val.find('E') == std::string::npos &&
            num >= -2147483648.0 && num <= 2147483647.0) {
            lua_pushinteger(L, (lua_Integer)num);
        } else {
            lua_pushnumber(L, num);
        }
        return;
    }

    // String
    lua_pushlstring(L, val.c_str(), val.size());
}

// Parse inline array: [a, b, c]
inline void pushInlineArray(lua_State* L, const std::string& val) {
    lua_newtable(L);
    // Strip brackets
    std::string inner = trim(val.substr(1, val.size() - 2));
    if (inner.empty()) return;

    int idx = 1;
    size_t pos = 0;
    while (pos < inner.size()) {
        // Find next comma (respecting quotes)
        size_t end = pos;
        bool inQ = false;
        char qChar = 0;
        while (end < inner.size()) {
            if (!inQ && (inner[end] == '"' || inner[end] == '\'')) {
                inQ = true; qChar = inner[end];
            } else if (inQ && inner[end] == qChar) {
                inQ = false;
            } else if (!inQ && inner[end] == ',') {
                break;
            }
            end++;
        }
        std::string item = trim(inner.substr(pos, end - pos));
        if (!item.empty()) {
            pushValue(L, item);
            lua_rawseti(L, -2, idx++);
        }
        pos = end + 1;
    }
}

// ─── Line representation ────────────────────────────────

struct Line {
    int indent;
    std::string key;       // empty for array items
    std::string value;     // empty for section headers
    bool isArrayItem;
    bool isSection;        // key: (no value, next lines are children)
};

inline std::vector<Line> tokenize(const std::string& text) {
    std::vector<Line> lines;
    std::istringstream stream(text);
    std::string raw;

    while (std::getline(stream, raw)) {
        // Strip trailing \r
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        int indent = measureIndent(raw);
        std::string trimmed = trim(raw);

        // Skip empty and comments
        if (trimmed.empty() || trimmed[0] == '#') continue;

        Line line;
        line.indent = indent;
        line.isArrayItem = false;
        line.isSection = false;

        // Array item: "- value" or "- key: value"
        if (trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == ' ') {
            line.isArrayItem = true;
            line.indent = indent; // indent of the dash
            std::string rest = trim(trimmed.substr(2));

            // Check if it's "- key: value" (map inside array)
            auto colonPos = rest.find(':');
            if (colonPos != std::string::npos && colonPos > 0 &&
                !isQuoted(rest) && rest[0] != '[') {
                // Could be a map entry like "- name: Alice"
                // But could also be "- http://url" — check for space after colon
                if (colonPos + 1 < rest.size() && rest[colonPos + 1] == ' ') {
                    line.key = trim(rest.substr(0, colonPos));
                    line.value = stripComment(trim(rest.substr(colonPos + 1)));
                    if (line.value.empty()) {
                        line.isSection = true;
                    }
                } else if (colonPos + 1 == rest.size()) {
                    // "- key:" with nothing after
                    line.key = trim(rest.substr(0, colonPos));
                    line.isSection = true;
                } else {
                    // Contains colon but no space after — treat as plain value
                    line.value = stripComment(rest);
                }
            } else {
                line.value = stripComment(rest);
            }
            lines.push_back(line);
            continue;
        }

        // Key: value pair
        auto colonPos = trimmed.find(':');
        if (colonPos != std::string::npos && colonPos > 0) {
            // Check it's not inside quotes
            if (!isQuoted(trimmed)) {
                line.key = trim(trimmed.substr(0, colonPos));
                std::string afterColon = trimmed.substr(colonPos + 1);
                std::string val = stripComment(trim(afterColon));

                if (val.empty()) {
                    line.isSection = true;
                } else {
                    line.value = val;
                }
                lines.push_back(line);
                continue;
            }
        }

        // Plain value (shouldn't happen in valid YAML at top level)
        line.value = stripComment(trimmed);
        lines.push_back(line);
    }

    return lines;
}

// ─── Recursive parser ───────────────────────────────────

// Forward declaration
inline int parseMap(lua_State* L, const std::vector<Line>& lines, int& pos, int parentIndent);
inline int parseArray(lua_State* L, const std::vector<Line>& lines, int& pos, int dashIndent);

// Parse a map (table with string keys) starting at pos
// Returns number of values pushed (1 = table on stack)
inline int parseMap(lua_State* L, const std::vector<Line>& lines, int& pos, int parentIndent) {
    lua_newtable(L);

    while (pos < (int)lines.size()) {
        const Line& line = lines[pos];

        // If indent <= parent, this line belongs to parent scope
        if (line.indent <= parentIndent && !line.isArrayItem) break;
        // Array items at same indent as a section header belong to that section
        if (line.isArrayItem && line.indent <= parentIndent) break;

        if (line.isSection && !line.key.empty()) {
            // Section header → check what follows
            int nextPos = pos + 1;
            if (nextPos < (int)lines.size() && lines[nextPos].isArrayItem &&
                lines[nextPos].indent > line.indent) {
                // Next is array items → parse as array
                lua_pushlstring(L, line.key.c_str(), line.key.size());
                pos++;
                parseArray(L, lines, pos, lines[pos].indent);
                lua_settable(L, -3);
            } else {
                // Next is map entries → recurse
                lua_pushlstring(L, line.key.c_str(), line.key.size());
                pos++;
                parseMap(L, lines, pos, line.indent);
                lua_settable(L, -3);
            }
        } else if (!line.key.empty()) {
            // key: value
            lua_pushlstring(L, line.key.c_str(), line.key.size());
            if (!line.value.empty() && line.value[0] == '[') {
                pushInlineArray(L, line.value);
            } else {
                pushValue(L, line.value);
            }
            lua_settable(L, -3);
            pos++;
        } else {
            // Unknown structure, skip
            pos++;
        }
    }

    return 1;
}

// Parse array items at dashIndent
inline int parseArray(lua_State* L, const std::vector<Line>& lines, int& pos, int dashIndent) {
    lua_newtable(L);
    int idx = 1;

    while (pos < (int)lines.size()) {
        const Line& line = lines[pos];

        // Stop if we go back to lower indent or non-array
        if (line.indent < dashIndent) break;
        if (line.indent == dashIndent && !line.isArrayItem) break;
        if (line.indent > dashIndent && !line.isArrayItem) {
            // Child of previous array item, skip for now
            pos++;
            continue;
        }

        if (!line.isArrayItem) break;
        if (line.indent != dashIndent) break;

        if (line.isSection && !line.key.empty()) {
            // Array of maps: "- name: Alice" followed by more keys
            // Create a sub-map
            lua_newtable(L); // the map for this array element
            lua_pushlstring(L, line.key.c_str(), line.key.size());
            // Check if section or value
            // It's a section within array item
            pos++;
            // Parse remaining keys at deeper indent into this map
            parseMap(L, lines, pos, line.indent);
            lua_settable(L, -3); // set sub-section into element map
            // Also, any peer keys at same indent+2 belong here
            // Actually this is tricky. Let's handle simpler case first.
            lua_rawseti(L, -2, idx++);
        } else if (!line.key.empty()) {
            // "- key: value" → array of maps
            // Start a new map for this array element
            lua_newtable(L);
            lua_pushlstring(L, line.key.c_str(), line.key.size());
            if (!line.value.empty() && line.value[0] == '[') {
                pushInlineArray(L, line.value);
            } else {
                pushValue(L, line.value);
            }
            lua_settable(L, -3);

            // Consume any following indented keys for this same map
            int itemIndent = line.indent;
            pos++;
            while (pos < (int)lines.size()) {
                const Line& next = lines[pos];
                if (next.indent <= itemIndent) break;
                if (next.isArrayItem) break;
                if (!next.key.empty() && !next.isSection) {
                    lua_pushlstring(L, next.key.c_str(), next.key.size());
                    if (!next.value.empty() && next.value[0] == '[') {
                        pushInlineArray(L, next.value);
                    } else {
                        pushValue(L, next.value);
                    }
                    lua_settable(L, -3);
                    pos++;
                } else if (next.isSection && !next.key.empty()) {
                    lua_pushlstring(L, next.key.c_str(), next.key.size());
                    pos++;
                    parseMap(L, lines, pos, next.indent);
                    lua_settable(L, -3);
                } else {
                    pos++;
                }
            }
            lua_rawseti(L, -2, idx++);
        } else {
            // Simple array item: "- value"
            if (!line.value.empty() && line.value[0] == '[') {
                pushInlineArray(L, line.value);
            } else {
                pushValue(L, line.value);
            }
            lua_rawseti(L, -2, idx++);
            pos++;
        }
    }

    return 1;
}

// ─── Main entry point ───────────────────────────────────

// Parse YAML text and push result table onto Lua stack.
// Returns 1 on success, 0 + pushes nil,error on failure.
inline int parseToLua(lua_State* L, const char* text) {
    if (!text || !*text) {
        lua_pushnil(L);
        lua_pushstring(L, "Empty YAML text");
        return 0;
    }

    auto lines = tokenize(text);
    if (lines.empty()) {
        lua_newtable(L); // empty table for empty YAML
        return 1;
    }

    int pos = 0;

    // Check if top-level is array
    if (lines[0].isArrayItem) {
        parseArray(L, lines, pos, lines[0].indent);
    } else {
        parseMap(L, lines, pos, -1);
    }

    return 1;
}

} // namespace YamlParser
