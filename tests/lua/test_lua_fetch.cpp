/**
 * Test: Lua Fetch
 * fetch() registration, BLE not connected handling, ble_connected() function
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"
#include "engines/lua/lua_engine.h"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== Lua Fetch Tests ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(240, 240);

    // Use full LuaEngine which registers fetch
    LuaEngine engine;
    engine.init();

    lua_State* L = engine.getLuaState();

    // === Registration ===
    printf("Registration:\n");

    TEST("fetch is function") {
        int err = luaL_dostring(L, "assert(type(fetch) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("ble_connected is function") {
        int err = luaL_dostring(L, "assert(type(ble_connected) == 'function')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    // === BLE not connected ===
    printf("\nBLE not connected:\n");

    TEST("ble_connected() returns false in mock") {
        int err = luaL_dostring(L, "assert(ble_connected() == false)");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("fetch with string URL — no crash when BLE down") {
        int err = luaL_dostring(L, "fetch('https://example.com')");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("fetch with table — no crash when BLE down") {
        int err = luaL_dostring(L, "fetch({url='https://example.com', method='GET'})");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("fetch with callback — receives error response") {
        int err = luaL_dostring(L,
            "local result = nil\n"
            "fetch('https://example.com', function(r)\n"
            "  result = r\n"
            "end)\n"
            "assert(result ~= nil, 'callback not called')\n"
            "assert(result.status == 0, 'status not 0: ' .. tostring(result.status))\n"
            "assert(result.ok == false, 'should not be ok')\n"
        );
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("fetch with table + callback — error response") {
        int err = luaL_dostring(L,
            "local result = nil\n"
            "fetch({url='https://example.com', method='POST', body='hello'}, function(r)\n"
            "  result = r\n"
            "end)\n"
            "assert(result ~= nil)\n"
            "assert(result.ok == false)\n"
        );
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    TEST("fetch without URL — no crash") {
        int err = luaL_dostring(L, "fetch({method='GET'})");
        if (err == 0) PASS(); else { FAIL(lua_tostring(L, -1)); lua_settop(L, 0); }
    }

    engine.shutdown();

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d LUA FETCH TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d LUA FETCH TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
