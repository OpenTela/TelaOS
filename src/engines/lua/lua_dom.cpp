/**
 * lua_dom.cpp — document and its elements for Lua (see lua_dom.h, docs/DOM.md).
 *
 * An element is a userdata holding its handle (UI::Dom::Node). The same element is the same Lua
 * value each time it is found, as in a browser: a weak table keeps the userdata of each handle.
 * el.classList is another userdata with the same handle: el.classList:add("on"); el.style too:
 * el.style.color = "red". The listeners of el:addEventListener() are kept in a registry table, by
 * handle and type; an event is a userdata that lives while its listeners run.
 */
#include "engines/lua/lua_dom.h"
#include "ui/dom.h"
#include "utils/log_config.h"

#include <string_view>

static const char* TAG = "LuaDom";

namespace LuaDom {
namespace {

constexpr const char* kElement = "TelaElement";
constexpr const char* kClassList = "TelaClassList";
constexpr const char* kStyle = "TelaStyle";
constexpr const char* kEvent = "TelaEvent";
constexpr const char* kKnown = "TelaOS.elements";       // handle → its userdata, weak
constexpr const char* kListeners = "TelaOS.listeners";  // handle → {type → {listener...}}

lua_State* s_L = nullptr;  // the state the listeners are in

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

// ---- querySelector ----

int badSelector(lua_State* L, const char* what, int at) {
    return luaL_error(L, "%s: '%s' is not a selector TelaOS reads", what, lua_tostring(L, at));
}

// The first element the selectors (argument at) match in `in` (0: the whole app); nil if none
int findFirst(lua_State* L, UI::Dom::Node in, int at) {
    UI::Dom::Node found = 0;
    if (!UI::Dom::querySelector(in, arg(L, at), found)) return badSelector(L, "querySelector", at);
    push(L, found);
    return 1;
}

// All of them, in the page order: a Lua array (#list, list[1], ipairs)
int findAll(lua_State* L, UI::Dom::Node in, int at) {
    P::Array<UI::Dom::Node> found;
    if (!UI::Dom::querySelectorAll(in, arg(L, at), found)) return badSelector(L, "querySelectorAll", at);
    lua_createtable(L, static_cast<int>(found.size()), 0);
    for (size_t k = 0; k < found.size(); ++k) {
        push(L, found[k]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(k + 1));
    }
    return 1;
}

// document.querySelector(s) and document:querySelector(s); el:querySelector(s) looks in el
int docQuerySelector(lua_State* L) { return findFirst(L, 0, lua_istable(L, 1) ? 2 : 1); }
int docQuerySelectorAll(lua_State* L) { return findAll(L, 0, lua_istable(L, 1) ? 2 : 1); }
int querySelector(lua_State* L) { return findFirst(L, self(L), 2); }
int querySelectorAll(lua_State* L) { return findAll(L, self(L), 2); }

// ---- Events ----

// The listeners n has for type, on the stack; with make, made if there are none. false: none
bool pushListeners(lua_State* L, UI::Dom::Node n, std::string_view type, bool make) {
    lua_getfield(L, LUA_REGISTRYINDEX, kListeners);
    if (lua_rawgeti(L, -1, static_cast<lua_Integer>(n)) != LUA_TTABLE) {
        lua_pop(L, 1);
        if (!make) {
            lua_pop(L, 1);
            return false;
        }
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, static_cast<lua_Integer>(n));
    }
    lua_pushlstring(L, type.data(), type.size());
    if (lua_rawget(L, -2) != LUA_TTABLE) {
        lua_pop(L, 1);
        if (!make) {
            lua_pop(L, 2);
            return false;
        }
        lua_newtable(L);
        lua_pushlstring(L, type.data(), type.size());
        lua_pushvalue(L, -2);
        lua_rawset(L, -4);
    }
    lua_replace(L, -3);  // the list, where the registry table was
    lua_pop(L, 1);
    return true;
}

// Where fn (at index f) is in the list on top; 0: not there
lua_Integer find(lua_State* L, int f) {
    const lua_Integer len = static_cast<lua_Integer>(lua_rawlen(L, -1));
    for (lua_Integer k = 1; k <= len; ++k) {
        lua_rawgeti(L, -1, k);
        const bool same = lua_rawequal(L, -1, f);
        lua_pop(L, 1);
        if (same) return k;
    }
    return 0;
}

// el:addEventListener(type, fn): once each, as in a browser
int addEventListener(lua_State* L) {
    const UI::Dom::Node n = self(L);
    const std::string_view type = arg(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    if (!UI::Dom::listen(n, type)) {
        return luaL_error(L, "addEventListener: TelaOS has no '%s' event", lua_tostring(L, 2));
    }
    pushListeners(L, n, type, true);
    if (!find(L, 3)) {
        lua_pushvalue(L, 3);
        lua_rawseti(L, -2, static_cast<lua_Integer>(lua_rawlen(L, -2)) + 1);
    }
    return 0;
}

int removeEventListener(lua_State* L) {
    const UI::Dom::Node n = self(L);
    const std::string_view type = arg(L, 2);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    if (!pushListeners(L, n, type, false)) return 0;
    const lua_Integer at = find(L, 3);
    if (!at) return 0;
    const lua_Integer len = static_cast<lua_Integer>(lua_rawlen(L, -1));
    for (lua_Integer k = at; k < len; ++k) {  // the ones after it move up
        lua_rawgeti(L, -1, k + 1);
        lua_rawseti(L, -2, k);
    }
    lua_pushnil(L);
    lua_rawseti(L, -2, len);
    return 0;
}

// An event: e.type, e.target, e.currentTarget, e.defaultPrevented, e:preventDefault(),
// e:stopPropagation(). Kept after its listeners ran, it has none of them
struct EventRef {
    UI::Dom::Event* ev;
};

UI::Dom::Event* eventOf(lua_State* L) { return static_cast<EventRef*>(luaL_checkudata(L, 1, kEvent))->ev; }

int preventDefault(lua_State* L) {
    if (UI::Dom::Event* ev = eventOf(L)) ev->defaultPrevented = true;
    return 0;
}

int stopPropagation(lua_State* L) {
    if (UI::Dom::Event* ev = eventOf(L)) ev->stopped = true;
    return 0;
}

int eventIndex(lua_State* L) {
    UI::Dom::Event* ev = eventOf(L);
    const std::string_view key = lua_type(L, 2) == LUA_TSTRING ? arg(L, 2) : std::string_view();
    if (key == "preventDefault") {
        lua_pushcfunction(L, preventDefault);
    } else if (key == "stopPropagation") {
        lua_pushcfunction(L, stopPropagation);
    } else if (!ev) {
        lua_pushnil(L);
    } else if (key == "type") {
        lua_pushlstring(L, ev->type.data(), ev->type.size());
    } else if (key == "target") {
        push(L, ev->target);
    } else if (key == "currentTarget") {
        push(L, ev->currentTarget);
    } else if (key == "defaultPrevented") {
        lua_pushboolean(L, ev->defaultPrevented);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

// UI::Dom: the listeners of ev.currentTarget, called in the order they were added. A list copied
// first: a listener may take itself off. One that fails is logged, the next still runs
void dispatch(UI::Dom::Event& ev) {
    lua_State* L = s_L;
    if (!L || !pushListeners(L, ev.currentTarget, ev.type, false)) return;
    const lua_Integer len = static_cast<lua_Integer>(lua_rawlen(L, -1));
    lua_createtable(L, static_cast<int>(len), 0);
    for (lua_Integer k = 1; k <= len; ++k) {
        lua_rawgeti(L, -2, k);
        lua_rawseti(L, -2, k);
    }
    auto* ref = static_cast<EventRef*>(lua_newuserdatauv(L, sizeof(EventRef), 0));
    ref->ev = &ev;
    luaL_setmetatable(L, kEvent);
    for (lua_Integer k = 1; k <= len; ++k) {
        lua_rawgeti(L, -2, k);
        lua_pushvalue(L, -2);
        if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
            LOG_E(Log::LUA, "%.*s listener: %s", static_cast<int>(ev.type.size()), ev.type.data(), lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    ref->ev = nullptr;
    lua_pop(L, 3);
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

// ---- style ----

UI::Dom::Node styleOf(lua_State* L) { return static_cast<Ref*>(luaL_checkudata(L, 1, kStyle))->node; }

void pushStyle(lua_State* L, UI::Dom::Node n) {
    auto* ref = static_cast<Ref*>(lua_newuserdatauv(L, sizeof(Ref), 0));
    ref->node = n;
    luaL_setmetatable(L, kStyle);
}

// A name as a script writes it, as CSS writes it: backgroundColor is background-color, cssFloat
// is float; background-color stays
P::String cssName(std::string_view key) {
    if (key == "cssFloat") return "float";
    P::String out;
    for (char c : key) {
        if (c >= 'A' && c <= 'Z') {
            out += '-';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

P::String propertyValue(UI::Dom::Node n, std::string_view name, bool* important = nullptr) {
    P::String value;
    bool imp = false;
    UI::Dom::styleProperty(n, name, value, imp);
    if (important) *important = imp;
    return value;
}

// setProperty(name, value[, "important"]); another priority leaves it alone, as in a browser
int styleSetProperty(lua_State* L) {
    const UI::Dom::Node n = styleOf(L);
    const std::string_view priority = lua_isnoneornil(L, 4) ? std::string_view() : arg(L, 4);
    if (!priority.empty() && priority != "important") return 0;
    UI::Dom::setStyleProperty(n, arg(L, 2), text(L, 3), !priority.empty());
    return 0;
}

int styleGetPropertyValue(lua_State* L) {
    pushString(L, propertyValue(styleOf(L), arg(L, 2)));
    return 1;
}

int styleGetPropertyPriority(lua_State* L) {
    bool important = false;
    propertyValue(styleOf(L), arg(L, 2), &important);
    lua_pushstring(L, important ? "important" : "");
    return 1;
}

// It gives back the value it had
int styleRemoveProperty(lua_State* L) {
    const UI::Dom::Node n = styleOf(L);
    const std::string_view name = arg(L, 2);
    pushString(L, propertyValue(n, name));
    UI::Dom::setStyleProperty(n, name, {});
    return 1;
}

// el.style.color, el.style.backgroundColor, el.style["background-color"]: "" if style="" has none
int styleIndex(lua_State* L) {
    const UI::Dom::Node n = styleOf(L);
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    const std::string_view key = arg(L, 2);
    if (key == "cssText") {
        pushString(L, UI::Dom::cssText(n));
        return 1;
    }
    lua_pushvalue(L, 2);
    if (lua_rawget(L, lua_upvalueindex(1)) != LUA_TNIL) return 1;  // a method
    lua_pop(L, 1);
    pushString(L, propertyValue(n, cssName(key)));
    return 1;
}

// el.style.color = "red"; "" or nil takes it off
int styleNewindex(lua_State* L) {
    const UI::Dom::Node n = styleOf(L);
    const std::string_view key = arg(L, 2);
    if (key == "cssText") UI::Dom::setCssText(n, text(L, 3));
    else UI::Dom::setStyleProperty(n, cssName(key), text(L, 3));
    return 0;
}

int styleString(lua_State* L) {
    pushString(L, UI::Dom::cssText(styleOf(L)));
    return 1;
}

const luaL_Reg kStyleMethods[] = {
    {"setProperty", styleSetProperty},
    {"getPropertyValue", styleGetPropertyValue},
    {"getPropertyPriority", styleGetPropertyPriority},
    {"removeProperty", styleRemoveProperty},
    {nullptr, nullptr},
};

const luaL_Reg kMethods[] = {
    {"getAttribute", getAttribute},
    {"setAttribute", setAttribute},
    {"removeAttribute", removeAttribute},
    {"hasAttribute", hasAttribute},
    {"querySelector", querySelector},
    {"querySelectorAll", querySelectorAll},
    {"addEventListener", addEventListener},
    {"removeEventListener", removeEventListener},
    {nullptr, nullptr},
};

// el.textContent, el.id, el.tagName, el.className, el.classList, el.style, el:method(); anything
// else is nil
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
    } else if (key == "style") {
        pushStyle(L, n);
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
    } else if (key == "style") {  // el.style = "color: red", as el.style.cssText
        UI::Dom::setCssText(n, text(L, 3));
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

    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kListeners);
    s_L = L;
    UI::Dom::setDispatch(dispatch);

    luaL_newmetatable(L, kEvent);
    lua_pushcfunction(L, eventIndex);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);

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

    luaL_newmetatable(L, kStyle);
    luaL_newlib(L, kStyleMethods);
    lua_pushcclosure(L, styleIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, styleNewindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, styleString);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    lua_newtable(L);
    lua_pushcfunction(L, getElementById);
    lua_setfield(L, -2, "getElementById");
    lua_pushcfunction(L, docQuerySelector);
    lua_setfield(L, -2, "querySelector");
    lua_pushcfunction(L, docQuerySelectorAll);
    lua_setfield(L, -2, "querySelectorAll");
    lua_setglobal(L, "document");
}

void shutdown(lua_State* L) {
    if (s_L != L) return;
    s_L = nullptr;
    UI::Dom::setDispatch(nullptr);
}

}  // namespace LuaDom
