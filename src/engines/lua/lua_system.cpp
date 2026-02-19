#include "engines/lua/lua_system.h"
#include "utils/log_config.h"
#include "ui/ui_engine.h"
#include <string>

namespace LuaSystem {

static const char* TAG = "LuaSystem";

static LaunchCallback g_launchCallback = nullptr;
static HomeCallback g_homeCallback = nullptr;

void setLaunchCallback(LaunchCallback cb) { g_launchCallback = cb; }
void setHomeCallback(HomeCallback cb) { g_homeCallback = cb; }

// app.exit([code[, msg]])
static int lua_app_exit(lua_State* L) {
    int code = (int)luaL_optinteger(L, 1, 0);
    const char* msg = luaL_optstring(L, 2, nullptr);
    
    if (msg) {
        LOG_I(Log::LUA, "[APP] exit(%d): %s", code, msg);
    } else {
        LOG_D(Log::LUA, "app.exit(%d)", code);
    }
    
    if (g_homeCallback) {
        g_homeCallback();
    }
    return 0;
}

// app.launch(name)
static int lua_app_launch(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    LOG_D(Log::LUA, "app.launch('%s')", name);
    if (g_launchCallback) {
        g_launchCallback(name);
    }
    return 0;
}

static const luaL_Reg app_lib[] = {
    {"exit",   lua_app_exit},
    {"launch", lua_app_launch},
    {nullptr, nullptr}
};

static uint32_t parseColor(lua_State* L, int idx) {
    uint32_t color;
    if (lua_isstring(L, idx)) {
        const char* str = lua_tostring(L, idx);
        if (str[0] == '#') str++;
        color = (uint32_t)strtoul(str, nullptr, 16);
    } else {
        color = (uint32_t)luaL_checkinteger(L, idx);
    }
    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;
    return (b << 16) | (g << 8) | r;
}

static int lua_canvas_clear(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    uint32_t color = parseColor(L, 2);
    UI::Engine::instance().canvasClear(id, color);
    return 0;
}

static int lua_canvas_rect(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    int w = (int)luaL_checkinteger(L, 4);
    int h = (int)luaL_checkinteger(L, 5);
    uint32_t color = parseColor(L, 6);
    UI::Engine::instance().canvasRect(id, x, y, w, h, color);
    return 0;
}

static int lua_canvas_pixel(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    int x = (int)luaL_checkinteger(L, 2);
    int y = (int)luaL_checkinteger(L, 3);
    uint32_t color = parseColor(L, 4);
    UI::Engine::instance().canvasPixel(id, x, y, color);
    return 0;
}

static int lua_canvas_circle(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    int cx = (int)luaL_checkinteger(L, 2);
    int cy = (int)luaL_checkinteger(L, 3);
    int r = (int)luaL_checkinteger(L, 4);
    uint32_t color = parseColor(L, 5);
    UI::Engine::instance().canvasCircle(id, cx, cy, r, color);
    return 0;
}

static int lua_canvas_line(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    int x1 = (int)luaL_checkinteger(L, 2);
    int y1 = (int)luaL_checkinteger(L, 3);
    int x2 = (int)luaL_checkinteger(L, 4);
    int y2 = (int)luaL_checkinteger(L, 5);
    uint32_t color = parseColor(L, 6);
    int thickness = (int)luaL_optinteger(L, 7, 1);
    UI::Engine::instance().canvasLine(id, x1, y1, x2, y2, color, thickness);
    return 0;
}

static int lua_canvas_refresh(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    UI::Engine::instance().canvasRefresh(id);
    return 0;
}

static const luaL_Reg canvas_lib[] = {
    {"clear", lua_canvas_clear},
    {"rect", lua_canvas_rect},
    {"pixel", lua_canvas_pixel},
    {"circle", lua_canvas_circle},
    {"line", lua_canvas_line},
    {"refresh", lua_canvas_refresh},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    // app.* namespace
    luaL_newlib(L, app_lib);
    lua_setglobal(L, "app");
    
    // Alias: exit()
    lua_register(L, "exit", lua_app_exit);
    
    // canvas.* namespace (unchanged)
    luaL_newlib(L, canvas_lib);
    lua_setglobal(L, "canvas");
    
    LOG_I(Log::LUA, "Registered: app.* + exit() + canvas.*");
}

} // namespace LuaSystem
