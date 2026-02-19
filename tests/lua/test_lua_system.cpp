/**
 * Test: Lua System Functions
 * app_launch(), app_home(), canvas.* from Lua scripts
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "engines/lua/lua_system.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

// Callback tracking
static std::string g_launched;
static int g_homeCount = 0;

static void onLaunch(const char* name) { g_launched = name; }
static void onHome() { g_homeCount++; }

int main() {
    printf("=== Lua System Tests ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(240, 240);

    // Create Lua state
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    // Register system functions
    LuaSystem::setLaunchCallback(onLaunch);
    LuaSystem::setHomeCallback(onHome);
    LuaSystem::registerAll(L);

    // === app_launch ===
    printf("app_launch:\n");

    TEST("app_launch('calculator')") {
        g_launched.clear();
        luaL_dostring(L, "app_launch('calculator')");
        if (g_launched == "calculator") PASS(); else FAIL(g_launched.c_str());
    }

    TEST("app_launch('settings')") {
        g_launched.clear();
        luaL_dostring(L, "app_launch('settings')");
        if (g_launched == "settings") PASS(); else FAIL(g_launched.c_str());
    }

    // === app_home ===
    printf("\napp_home:\n");

    TEST("app_home() calls callback") {
        g_homeCount = 0;
        luaL_dostring(L, "app_home()");
        if (g_homeCount == 1) PASS(); else { FAIL(""); printf("      count=%d\n", g_homeCount); }
    }

    TEST("app_home() twice") {
        g_homeCount = 0;
        luaL_dostring(L, "app_home() app_home()");
        if (g_homeCount == 2) PASS(); else { FAIL(""); printf("      count=%d\n", g_homeCount); }
    }

    // === canvas table registered ===
    printf("\ncanvas:\n");

    TEST("canvas table exists") {
        luaL_dostring(L, "assert(type(canvas) == 'table')");
        if (lua_gettop(L) == 0) PASS(); else { FAIL("error on stack"); lua_settop(L, 0); }
    }

    TEST("canvas.clear is function") {
        int err = luaL_dostring(L, "assert(type(canvas.clear) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("canvas.rect is function") {
        int err = luaL_dostring(L, "assert(type(canvas.rect) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("canvas.pixel is function") {
        int err = luaL_dostring(L, "assert(type(canvas.pixel) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("canvas.line is function") {
        int err = luaL_dostring(L, "assert(type(canvas.line) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("canvas.circle is function") {
        int err = luaL_dostring(L, "assert(type(canvas.circle) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("canvas.refresh is function") {
        int err = luaL_dostring(L, "assert(type(canvas.refresh) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    // === freezeUI / unfreezeUI ===
    printf("\nfreezeUI:\n");

    TEST("freezeUI exists and callable") {
        int err = luaL_dostring(L, "freezeUI()");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("unfreezeUI exists and callable") {
        int err = luaL_dostring(L, "unfreezeUI()");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    // === no callback — should not crash ===
    printf("\nno callback safety:\n");

    TEST("app_launch without callback — no crash") {
        LuaSystem::setLaunchCallback(nullptr);
        int err = luaL_dostring(L, "app_launch('test')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
        LuaSystem::setLaunchCallback(onLaunch);
    }

    TEST("app_home without callback — no crash") {
        LuaSystem::setHomeCallback(nullptr);
        int err = luaL_dostring(L, "app_home()");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
        LuaSystem::setHomeCallback(onHome);
    }

    lua_close(L);

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d LUA SYSTEM TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d LUA SYSTEM TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
