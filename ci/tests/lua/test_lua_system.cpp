/**
 * Test: Lua System Functions (new API)
 * app.launch(name), app.exit([code, msg]), exit() alias
 * canvas.* (unchanged)
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "engines/lua/lua_system.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static std::string g_launched;
static int g_homeCount = 0;

static void onLaunch(const char* name) { g_launched = name; }
static void onHome() { g_homeCount++; }

int main() {
    printf("=== Lua System Tests (new API) ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(240, 240);

    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    LuaSystem::setLaunchCallback(onLaunch);
    LuaSystem::setHomeCallback(onHome);
    LuaSystem::registerAll(L);

    // === app namespace ===
    printf("app namespace:\n");

    TEST("app is a table") {
        int err = luaL_dostring(L, "assert(type(app) == 'table')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("app.launch is function") {
        int err = luaL_dostring(L, "assert(type(app.launch) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("app.exit is function") {
        int err = luaL_dostring(L, "assert(type(app.exit) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("exit() alias is function") {
        int err = luaL_dostring(L, "assert(type(exit) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    // === app.launch ===
    printf("\napp.launch:\n");

    TEST("app.launch('calculator')") {
        g_launched.clear();
        luaL_dostring(L, "app.launch('calculator')");
        if (g_launched == "calculator") PASS(); else FAIL(g_launched.c_str());
    }

    TEST("app.launch('settings')") {
        g_launched.clear();
        luaL_dostring(L, "app.launch('settings')");
        if (g_launched == "settings") PASS(); else FAIL(g_launched.c_str());
    }

    // === app.exit ===
    printf("\napp.exit:\n");

    TEST("app.exit() calls home callback") {
        g_homeCount = 0;
        luaL_dostring(L, "app.exit()");
        if (g_homeCount == 1) PASS(); else FAIL("wrong count");
    }

    TEST("exit() alias calls home callback") {
        g_homeCount = 0;
        luaL_dostring(L, "exit()");
        if (g_homeCount == 1) PASS(); else FAIL("wrong count");
    }

    TEST("exit(0) with code") {
        g_homeCount = 0;
        int err = luaL_dostring(L, "exit(0)");
        if (err == 0 && g_homeCount == 1) PASS(); else FAIL("error or no callback");
    }

    TEST("exit(1, 'Error: test') with code+msg") {
        g_homeCount = 0;
        int err = luaL_dostring(L, "exit(1, 'Error: test')");
        if (err == 0 && g_homeCount == 1) PASS(); else FAIL("error or no callback");
    }

    TEST("exit(0, 'Done!') success+msg") {
        g_homeCount = 0;
        int err = luaL_dostring(L, "exit(0, 'Done!')");
        if (err == 0 && g_homeCount == 1) PASS(); else FAIL("error or no callback");
    }

    // === canvas (unchanged) ===
    printf("\ncanvas:\n");

    TEST("canvas table exists") {
        int err = luaL_dostring(L, "assert(type(canvas) == 'table')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
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

    // === no callback safety ===
    printf("\nno callback safety:\n");

    TEST("app.launch without callback — no crash") {
        LuaSystem::setLaunchCallback(nullptr);
        int err = luaL_dostring(L, "app.launch('test')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
        LuaSystem::setLaunchCallback(onLaunch);
    }

    TEST("app.exit without callback — no crash") {
        LuaSystem::setHomeCallback(nullptr);
        int err = luaL_dostring(L, "app.exit()");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
        LuaSystem::setHomeCallback(onHome);
    }

    lua_close(L);

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d LUA SYSTEM TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d LUA SYSTEM TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
