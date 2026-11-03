/**
 * lua_dom.cpp — document and its elements for Lua (see lua_dom.h, docs/DOM.md).
 *
 * An element is a userdata holding its handle (UI::Dom::Node). The same element is the same Lua
 * value each time it is found, as in a browser: a weak table keeps the userdata of each handle.
 * el.classList is another userdata with the same handle: el.classList:add("on").
 */
#include "engines/lua/lua_dom.h"
#include "ui/dom.h"

#include <string_view>

namespace LuaDom {
namespace {

constexpr const char* kElement = "TelaElement";
constexpr const char* kClassList = "TelaClassList";
constexpr const char* kKnown = "TelaOS.elements";  // handle → its userdata, weak

struct Ref {
    UI::Dom::Node node;
};

void push(lua_State* L, UI::Dom::Node n) {
    if (!n) {
        lua_pushnil(L);
        return;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kKnown);
    if (lua_rawgeti(L, -1, static_cast<lua_Integer>(n)) != LUA_TNIL) {
        lua_remove(L, -2);
        return;
    }
    lua_pop(L, 1);
    auto* ref = static_cast<Ref*>(lua_newuserdatauv(L, sizeof(Ref), 0));
    ref->node = n;
    luaL_setmetatable(L, kElement);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, static_cast<lua_Integer>(n));
    lua_remove(L, -2);
}

UI::Dom::Node self(lua_State* L) { return static_cast<Ref*>(luaL_checkudata(L, 1, kElement))->node; }

std::string_view arg(lua_State* L, int i) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, i, &len);
    return {s, len};
}

// A value as text, as the DOM turns it into a string; nil is "" (a boolean attribute, no text)
std::string_view text(lua_State* L, int i) {
    if (lua_isnoneornil(L, i)) return {};
    size_t len = 0;
    const char* s = luaL_tolstring(L, i, &len);  // stays on the stack while it is used
    return {s, len};
}

void pushString(lua_State* L, const P::String& s) { lua_pushlstring(L, s.data(), s.size()); }

int getElementById(lua_State* L) {
    const int at = lua_istable(L, 1) ? 2 : 1;  // document:getElementById(id) too
    push(L, UI::Dom::getElementById(arg(L, at)));
    return 1;
}

int getAttribute(lua_State* L) {
    P::String value;
    if (UI::Dom::getAttribute(self(L), arg(L, 2), value)) pushString(L, value);
    else lua_pushnil(L);
    return 1;
}

int setAttribute(lua_State* L) {
    const std::string_view name = arg(L, 2);
    if (!UI::Dom::setAttribute(self(L), name, text(L, 3))) {
        return luaL_error(L, "setAttribute: '%s' is not an attribute name", lua_tostring(L, 2));
    }
    return 0;
}

int removeAttribute(lua_State* L) {
    UI::Dom::removeAttribute(self(L), arg(L, 2));
    return 0;
}

int hasAttribute(lua_State* L) {
    lua_pushboolean(L, UI::Dom::hasAttribute(self(L), arg(L, 2)));
    return 1;
}

// ---- classList ----

UI::Dom::Node listOf(lua_State* L) { return static_cast<Ref*>(luaL_checkudata(L, 1, kClassList))->node; }

void pushList(lua_State* L, UI::Dom::Node n) {
    auto* ref = static_cast<Ref*>(lua_newuserdatauv(L, sizeof(Ref), 0));
    ref->node = n;
    luaL_setmetatable(L, kClassList);
}

P::Array<std::string_view> names(lua_State* L) {
    P::Array<std::string_view> out;
    for (int i = 2; i <= lua_gettop(L); ++i) out.push_back(arg(L, i));
    return out;
}

int badName(lua_State* L, const char* what) {
    return luaL_error(L, "classList.%s: a class name is empty or has a space in it", what);
}

int classAdd(lua_State* L) {
    const UI::Dom::Node n = listOf(L);
    return UI::Dom::addClasses(n, names(L)) ? 0 : badName(L, "add");
}

int classRemove(lua_State* L) {
    const UI::Dom::Node n = listOf(L);
    return UI::Dom::removeClasses(n, names(L)) ? 0 : badName(L, "remove");
}

// toggle(name): the other way; toggle(name, true|false): on or off. It says whether it is on now
int classToggle(lua_State* L) {
    const UI::Dom::Node n = listOf(L);
    const std::string_view name = arg(L, 2);
    const int force = lua_isnoneornil(L, 3) ? -1 : (lua_toboolean(L, 3) ? 1 : 0);
    bool now = false;
    if (!UI::Dom::toggleClass(n, name, force, now)) return badName(L, "toggle");
    lua_pushboolean(L, now);
    return 1;
}

int classContains(lua_State* L) {
    lua_pushboolean(L, UI::Dom::hasClass(listOf(L), arg(L, 2)));
    return 1;
}

int classString(lua_State* L) {
    pushString(L, UI::Dom::className(listOf(L)));
    return 1;
}

const luaL_Reg kClassMethods[] = {
    {"add", classAdd},
    {"remove", classRemove},
    {"toggle", classToggle},
    {"contains", classContains},
    {nullptr, nullptr},
};

const luaL_Reg kMethods[] = {
    {"getAttribute", getAttribute},
    {"setAttribute", setAttribute},
    {"removeAttribute", removeAttribute},
    {"hasAttribute", hasAttribute},
    {nullptr, nullptr},
};

// el.textContent, el.id, el.tagName, el.className, el.classList, el:method(); anything else is nil
int index(lua_State* L) {
    const UI::Dom::Node n = self(L);
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    const std::string_view key = arg(L, 2);
    if (key == "textContent") {
        pushString(L, UI::Dom::textContent(n));
    } else if (key == "id") {
        P::String id;
        UI::Dom::getAttribute(n, "id", id);
        pushString(L, id);
    } else if (key == "tagName") {
        pushString(L, UI::Dom::tagName(n));
    } else if (key == "className") {
        pushString(L, UI::Dom::className(n));
    } else if (key == "classList") {
        pushList(L, n);
    } else {
        lua_pushvalue(L, 2);
        lua_rawget(L, lua_upvalueindex(1));  // the methods
    }
    return 1;
}

int newindex(lua_State* L) {
    const UI::Dom::Node n = self(L);
    const std::string_view key = arg(L, 2);
    if (key == "textContent") {
        UI::Dom::setTextContent(n, text(L, 3));
    } else if (key == "id") {
        UI::Dom::setAttribute(n, "id", text(L, 3));
    } else if (key == "className") {
        UI::Dom::setAttribute(n, "class", text(L, 3));
    } else {
        return luaL_error(L, "an element has no '%s' to set", lua_tostring(L, 2));
    }
    return 0;
}

int tostring(lua_State* L) {
    const UI::Dom::Node n = self(L);
    P::String s = "<" + UI::Dom::tagName(n);
    P::String id;
    if (UI::Dom::getAttribute(n, "id", id) && !id.empty()) s += " id=\"" + id + "\"";
    s += ">";
    pushString(L, s);
    return 1;
}

}  // namespace

void registerAll(lua_State* L) {
    lua_newtable(L);  // the elements found so far, weak: an element nothing holds is let go
    lua_newtable(L);
    lua_pushstring(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    lua_setfield(L, LUA_REGISTRYINDEX, kKnown);

    luaL_newmetatable(L, kElement);
    luaL_newlib(L, kMethods);
    lua_pushcclosure(L, index, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    luaL_newmetatable(L, kClassList);
    luaL_newlib(L, kClassMethods);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, classString);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushcfunction(L, getElementById);
    lua_setfield(L, -2, "getElementById");
    lua_setglobal(L, "document");
}

}  // namespace LuaDom
