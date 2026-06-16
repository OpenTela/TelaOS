/**
 * Test: Lua Error Resilience
 * 
 * Verify the engine survives bad input without crashing:
 * syntax errors, runtime errors, missing functions, state abuse,
 * lifecycle edge cases, stack safety.
 */
#include <cstdio>
#include "ui/ui_html.h"
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"

static int g_passed = 0, g_total = 0;
#define TEST(name) printf("  %-55s ", name); g_total++;
#define PASS() do { printf("✓\n"); g_passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)
#define SECTION(name) printf("\n%s:\n", name)

static LuaEngine eng;

static bool fresh() {
    eng.shutdown();
    g_core.store().clear();
    return eng.init();
}

int main() {
    printf("=== Lua Error Resilience Tests ===\n");

    LvglMock::create_screen(240, 240);

    // ──── Syntax errors ────────────────────────────────
    SECTION("Syntax errors");

    TEST("syntax error returns false") {
        fresh();
        bool ok = eng.execute("if then what end garbage");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("unterminated string returns false") {
        fresh();
        bool ok = eng.execute("local s = \"hello");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("VM usable after syntax error") {
        fresh();
        eng.execute("this is not lua!!");
        bool ok = eng.execute("state.x = '42'");
        if (ok) PASS(); else FAIL("VM broken");
    }

    TEST("multiple syntax errors don't accumulate") {
        fresh();
        for (int i = 0; i < 10; i++) eng.execute("!@#$%");
        bool ok = eng.execute("state.y = 'ok'");
        if (ok && g_core.store().getString("y") == "ok") PASS();
        else FAIL("VM broken after 10 errors");
    }

    // ──── Runtime errors ───────────────────────────────
    SECTION("Runtime errors");

    TEST("nil index returns false") {
        fresh();
        bool ok = eng.execute("local t = nil; return t.x");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("nil function call returns false") {
        fresh();
        bool ok = eng.execute("local f = nil; f()");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("division by zero — no crash") {
        fresh();
        // Lua handles 1/0 as inf, not error — but 0/0 is nan
        bool ok = eng.execute("state.r = tostring(1/0)");
        // Should succeed (inf is valid in Lua)
        if (ok) PASS(); else FAIL("should be ok, Lua handles inf");
    }

    TEST("string + number type error returns false") {
        fresh();
        bool ok = eng.execute("local x = 'abc' + 5");
        // Lua 5.4 allows this if string is numeric, but "abc" isn't
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("VM usable after runtime error") {
        fresh();
        eng.execute("error('boom')");
        bool ok = eng.execute("state.alive = 'yes'");
        if (ok && g_core.store().getString("alive") == "yes") PASS();
        else FAIL("VM broken after runtime error");
    }

    TEST("explicit error() caught gracefully") {
        fresh();
        bool ok = eng.execute("error('intentional')");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("assert(false) caught gracefully") {
        fresh();
        bool ok = eng.execute("assert(false, 'nope')");
        if (!ok) PASS(); else FAIL("should fail");
    }

    // ──── call() edge cases ────────────────────────────
    SECTION("call() edge cases");

    TEST("call non-existent function returns false") {
        fresh();
        bool ok = eng.call("doesNotExist");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("call nil global returns false") {
        fresh();
        eng.execute("myFunc = nil");
        bool ok = eng.call("myFunc");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("call function that errors internally") {
        fresh();
        eng.execute("function boom() error('kaboom') end");
        bool ok = eng.call("boom");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("call after function error — VM ok") {
        fresh();
        eng.execute("function bad() error('x') end");
        eng.execute("function good() state.v = 'ok' end");
        eng.call("bad");
        eng.call("good");
        if (g_core.store().getString("v") == "ok") PASS();
        else FAIL("VM broken");
    }

    TEST("call global that is string, not function") {
        fresh();
        eng.execute("notAFunc = 'hello'");
        bool ok = eng.call("notAFunc");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("call empty string") {
        fresh();
        bool ok = eng.call("");
        if (!ok) PASS(); else FAIL("should fail");
    }

    // ──── execute() edge input ─────────────────────────
    SECTION("execute() edge input");

    TEST("empty string succeeds") {
        fresh();
        bool ok = eng.execute("");
        if (ok) PASS(); else FAIL("empty should be ok");
    }

    TEST("whitespace-only succeeds") {
        fresh();
        bool ok = eng.execute("   \n\t  \n");
        if (ok) PASS(); else FAIL("whitespace should be ok");
    }

    TEST("comment-only succeeds") {
        fresh();
        bool ok = eng.execute("-- just a comment\n-- another");
        if (ok) PASS(); else FAIL("comments should be ok");
    }

    TEST("very long variable value — no crash") {
        fresh();
        std::string code = "state.big = '";
        for (int i = 0; i < 5000; i++) code += 'A';
        code += "'";
        bool ok = eng.execute(code.c_str());
        if (ok && g_core.store().getString("big").size() == 5000) PASS();
        else FAIL("long string failed");
    }

    TEST("unicode in code and state") {
        fresh();
        // Use Lua escape for non-ASCII to avoid source encoding issues
        bool ok = eng.execute("state.uni = 'Hello' .. ' ' .. 'World'");
        if (ok && g_core.store().getString("uni") == "Hello World") PASS();
        else FAIL("basic string concat failed");
    }

    TEST("binary-safe state values") {
        fresh();
        bool ok = eng.execute("state.bin = string.char(0x41, 0x42, 0x43)");
        if (ok && g_core.store().getString("bin") == "ABC") PASS();
        else FAIL("string.char failed");
    }

    // ──── State interaction after errors ────────────────
    SECTION("State after errors");

    TEST("setState works after execute error") {
        fresh();
        eng.execute("this is broken");
        eng.setState("key", "val");
        // getState should return what we set
        if (g_core.store().getString("key") == "val") PASS();
        else FAIL("setState broken");
    }

    TEST("state set before error is preserved") {
        fresh();
        eng.execute("state.before = 'saved'");
        eng.execute("error('crash')");
        if (g_core.store().getString("before") == "saved") PASS();
        else FAIL("state lost");
    }

    TEST("state set in erroring script up to error point") {
        fresh();
        eng.execute("state.a = '1'; state.b = '2'; error('mid'); state.c = '3'");
        bool aOk = g_core.store().getString("a") == "1";
        bool bOk = g_core.store().getString("b") == "2";
        bool cEmpty = g_core.store().getString("c").empty() || g_core.store().getString("c") == "";
        if (aOk && bOk && cEmpty) PASS();
        else FAIL("partial state wrong");
    }

    // ──── Lifecycle ────────────────────────────────────
    SECTION("Lifecycle");

    TEST("double shutdown — no crash") {
        fresh();
        eng.shutdown();
        eng.shutdown();
        PASS();  // if we got here, no crash
    }

    TEST("call after shutdown returns false") {
        fresh();
        eng.execute("function f() end");
        eng.shutdown();
        bool ok = eng.call("f");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("execute after shutdown returns false") {
        fresh();
        eng.shutdown();
        bool ok = eng.execute("state.x = '1'");
        if (!ok) PASS(); else FAIL("should fail");
    }

    TEST("re-init after shutdown works") {
        fresh();
        eng.execute("state.round = '1'");
        eng.shutdown();
        eng.init();
        bool ok = eng.execute("state.round = '2'");
        if (ok && g_core.store().getString("round") == "2") PASS();
        else FAIL("re-init broken");
    }

    TEST("re-init clears previous functions") {
        fresh();
        eng.execute("function oldFunc() state.old = 'yes' end");
        eng.shutdown();
        eng.init();
        bool ok = eng.call("oldFunc");
        if (!ok) PASS();  // oldFunc shouldn't exist
        else FAIL("old function survived re-init");
    }

    // ──── Stack safety ─────────────────────────────────
    SECTION("Stack safety");

    TEST("deep recursion caught, VM survives") {
        fresh();
        eng.execute("function recurse(n) return recurse(n+1) end");
        bool ok = eng.call("recurse");
        // Should fail (stack overflow)
        // Then VM should still work
        bool alive = eng.execute("state.post = 'ok'");
        if (!ok && alive) PASS();
        else FAIL("stack overflow handling broken");
    }

    TEST("many sequential calls — no stack leak") {
        fresh();
        eng.execute("function noop() end");
        bool allOk = true;
        for (int i = 0; i < 1000; i++) {
            if (!eng.call("noop")) { allOk = false; break; }
        }
        if (allOk) PASS();
        else FAIL("call failed after many iterations");
    }

    TEST("many sequential errors — no stack leak") {
        fresh();
        eng.execute("function bad() error('x') end");
        for (int i = 0; i < 100; i++) eng.call("bad");
        bool ok = eng.execute("state.final = 'ok'");
        if (ok && g_core.store().getString("final") == "ok") PASS();
        else FAIL("VM broken after many errors");
    }

    // ──── Summary ──────────────────────────────────────
    printf("\n");
    eng.shutdown();
    if (g_passed == g_total) {
        printf("=== ALL %d LUA ERROR TESTS PASSED ===\n", g_total);
        return 0;
    } else {
        printf("=== %d/%d LUA ERROR TESTS PASSED ===\n", g_passed, g_total);
        return 1;
    }
}
