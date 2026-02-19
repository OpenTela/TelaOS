#include "engines/lua/lua_ui.h"
#include "utils/log_config.h"
#include "utils/psram_alloc.h"
#include "ui/ui_engine.h"

// Forward declarations - implemented in ui_engine.cpp
namespace UI {
    bool setWidgetAttr(const char* id, const char* attr, const char* value);
    P::String getWidgetAttr(const char* id, const char* attr);
    bool focusInput(const char* id);
}

namespace LuaUI {

static const char* TAG = "LuaUI";

static int lua_navigate(lua_State* L) {
    const char* page = luaL_checkstring(L, 1);
    UI::Engine::instance().showPage(page);
    return 0;
}

static int lua_focus(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    bool ok = UI::focusInput(id);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int lua_setAttr(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* attr = luaL_checkstring(L, 2);
    const char* value = luaL_checkstring(L, 3);
    bool ok = UI::setWidgetAttr(id, attr, value);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int lua_getAttr(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* attr = luaL_checkstring(L, 2);
    P::String val = UI::getWidgetAttr(id, attr);
    lua_pushstring(L, val.c_str());
    return 1;
}

static int lua_freeze(lua_State* L) {
    return 0;
}

static int lua_unfreeze(lua_State* L) {
    return 0;
}

static const luaL_Reg ui_lib[] = {
    {"navigate", lua_navigate},
    {"setAttr",  lua_setAttr},
    {"getAttr",  lua_getAttr},
    {"focus",    lua_focus},
    {"freeze",   lua_freeze},
    {"unfreeze", lua_unfreeze},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    // ui.* namespace
    luaL_newlib(L, ui_lib);
    lua_setglobal(L, "ui");
    
    // Aliases for backward compat
    lua_register(L, "navigate", lua_navigate);
    lua_register(L, "focus", lua_focus);
    lua_register(L, "setAttr", lua_setAttr);
    lua_register(L, "getAttr", lua_getAttr);
    
    LOG_I(Log::LUA, "Registered: ui.* + navigate/focus/setAttr/getAttr()");
}

} // namespace LuaUI
