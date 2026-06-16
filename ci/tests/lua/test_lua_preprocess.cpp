/**
 * Test: Lua forward declarations (e2e with real Lua VM)
 * 
 * Verifies that local functions defined BELOW can be called from ABOVE.
 * Uses real Lua 5.4 — not stubs.
 * 
 * Compile: see test_lua_preprocess.sh
 */
#include <cstdio>
#include "ui/ui_html.h"
#include <cstring>

#include "engines/lua/lua_engine.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

int main() {
    printf("=== Lua Forward Declaration Tests (e2e) ===\n\n");
    int passed = 0;
    int total = 0;
    
    LuaEngine engine;
    engine.init();
    g_core.store().clear();
    
    // === The original bug ===
    printf("original bug:\n");
    
    TEST("local function called before definition");
    {
        bool ok = engine.execute(
            "local function initColors()\n"
            "    state.color = 'red'\n"
            "end\n"
            "\n"
            "function restart()\n"
            "    initColors()\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            ok = engine.call("restart");
            if (ok && g_core.store().getString("color") == "red") PASS();
            else FAIL("initColors not called");
        }
    }
    
    // Reset
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("local defined below, called from above");
    {
        bool ok = engine.execute(
            "function doWork()\n"
            "    helper()\n"
            "end\n"
            "\n"
            "local function helper()\n"
            "    state.result = 'ok'\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            ok = engine.call("doWork");
            if (ok && g_core.store().getString("result") == "ok") PASS();
            else FAIL("helper not called");
        }
    }
    
    // === Mutual local functions ===
    printf("\nmutual calls:\n");
    
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("two local functions calling each other");
    {
        bool ok = engine.execute(
            "local function b()\n"
            "    state.trace = state.trace .. 'B'\n"
            "end\n"
            "\n"
            "local function a()\n"
            "    state.trace = state.trace .. 'A'\n"
            "    b()\n"
            "end\n"
            "\n"
            "function run()\n"
            "    state.trace = ''\n"
            "    a()\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            engine.call("run");
            auto trace = g_core.store().getString("trace");
            if (trace == "AB") PASS();
            else { FAIL("wrong trace"); printf("      got: '%s'\n", trace.c_str()); }
        }
    }
    
    // === Mixed: global + local ===
    printf("\nmixed global/local:\n");
    
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("global function not broken by preprocessor");
    {
        bool ok = engine.execute(
            "function globalFn()\n"
            "    state.g = 'yes'\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            engine.call("globalFn");
            if (g_core.store().getString("g") == "yes") PASS();
            else FAIL("global broken");
        }
    }
    
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("local + global coexist");
    {
        bool ok = engine.execute(
            "local function localHelper()\n"
            "    state.who = 'local'\n"
            "end\n"
            "\n"
            "function globalEntry()\n"
            "    localHelper()\n"
            "    state.who = state.who .. '+global'\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            engine.call("globalEntry");
            auto who = g_core.store().getString("who");
            if (who == "local+global") PASS();
            else { FAIL("wrong result"); printf("      got: '%s'\n", who.c_str()); }
        }
    }
    
    // === Params preserved ===
    printf("\nparams:\n");
    
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("local function params work");
    {
        bool ok = engine.execute(
            "local function add(a, b)\n"
            "    state.sum = tostring(a + b)\n"
            "end\n"
            "\n"
            "function calc()\n"
            "    add(17, 25)\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            engine.call("calc");
            if (g_core.store().getString("sum") == "42") PASS();
            else { FAIL("wrong sum"); printf("      got: '%s'\n", g_core.store().getString("sum").c_str()); }
        }
    }
    
    // === Recursion ===
    printf("\nrecursion:\n");
    
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("recursive local function");
    {
        bool ok = engine.execute(
            "local function factorial(n)\n"
            "    if n <= 1 then return 1 end\n"
            "    return n * factorial(n - 1)\n"
            "end\n"
            "\n"
            "function calcFactorial()\n"
            "    state.fact = tostring(factorial(6))\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            engine.call("calcFactorial");
            if (g_core.store().getString("fact") == "720") PASS();
            else { FAIL("wrong factorial"); printf("      got: '%s'\n", g_core.store().getString("fact").c_str()); }
        }
    }
    
    // === No local functions — no changes ===
    printf("\nno-op:\n");
    
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("code without local functions works fine");
    {
        bool ok = engine.execute(
            "function simple()\n"
            "    state.val = 'works'\n"
            "end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            engine.call("simple");
            if (g_core.store().getString("val") == "works") PASS();
            else FAIL("simple broken");
        }
    }
    
    // === Indented (from <script> in HTML) ===
    printf("\nindented:\n");
    
    engine.shutdown();
    engine.init();
    g_core.store().clear();
    
    TEST("indented local function (HTML script tag)");
    {
        bool ok = engine.execute(
            "    local function setup()\n"
            "        state.init = 'done'\n"
            "    end\n"
            "\n"
            "    function boot()\n"
            "        setup()\n"
            "    end\n"
        );
        if (!ok) { FAIL("execute failed"); }
        else {
            engine.call("boot");
            if (g_core.store().getString("init") == "done") PASS();
            else FAIL("indented not handled");
        }
    }
    
    engine.shutdown();
    
    // Summary
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d TESTS FAILED ===\n", total - passed, total);
        return 1;
    }
}
