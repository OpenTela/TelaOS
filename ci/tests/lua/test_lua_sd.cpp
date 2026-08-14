/**
 * Test: sd.* Lua API — unmounted behaviour (mock)
 *
 * The mock build stubs Sd:: so nothing ever mounts. What we verify here is
 * the wrapper's contract when the card is absent:
 * - every function exists and is callable
 * - I/O ops return nil + "sd: not mounted" instead of crashing
 * - boolean probes (mounted/exists/isDir) return false
 * Real FS behaviour is exercised on hardware; this locks the API shape.
 */
#include <cstdio>
#include <cstring>
#include "ui/ui_html.h"
#include "lvgl.h"
#include "lvgl_mock.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"

static int g_passed = 0, g_total = 0;
#define TEST(name) printf("  %-55s ", name); g_total++;
#define PASS() do { printf("\u2713\n"); g_passed++; } while(0)
#define FAIL(msg) printf("\u2717 %s\n", msg)

static LuaEngine eng;

// Run a Lua chunk that sets global `ok` (boolean). Returns its value.
static bool luaOk(const char* code) {
    if (!eng.execute(code)) return false;
    // read global 'ok'
    lua_State* L = eng.getLuaState();
    lua_getglobal(L, "ok");
    bool v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

int main() {
    printf("=== sd.* API (unmounted mock) ===\n\n");
    LvglMock::create_screen(240, 240);
    g_core.store().clear();
    eng.init();

    TEST("sd table registered with all 15 functions");
    if (luaOk(R"(
        local fns = {"mounted","mount","unmount","info","list","read","write",
                     "append","exists","isDir","stat","remove","mkdir","rename","copy"}
        ok = true
        for _,f in ipairs(fns) do
            if type(sd[f]) ~= "function" then ok = false end
        end
    )")) PASS(); else FAIL("missing functions");

    TEST("sd.mounted() == false");
    if (luaOk("ok = (sd.mounted() == false)")) PASS(); else FAIL("expected false");

    TEST("sd.exists/isDir return false, no error");
    if (luaOk(R"(ok = (sd.exists("/x")==false) and (sd.isDir("/x")==false))")) PASS(); else FAIL("bad");

    TEST("sd.list -> nil, 'sd: not mounted'");
    if (luaOk(R"(local r,e = sd.list("/"); ok = (r==nil and e=="sd: not mounted"))")) PASS(); else FAIL("bad");

    TEST("sd.read -> nil, 'sd: not mounted'");
    if (luaOk(R"(local r,e = sd.read("/a"); ok = (r==nil and e=="sd: not mounted"))")) PASS(); else FAIL("bad");

    TEST("sd.write -> nil, 'sd: not mounted'");
    if (luaOk(R"(local r,e = sd.write("/a","x"); ok = (r==nil and e=="sd: not mounted"))")) PASS(); else FAIL("bad");

    TEST("sd.stat -> nil, 'sd: not mounted'");
    if (luaOk(R"(local r,e = sd.stat("/a"); ok = (r==nil and e=="sd: not mounted"))")) PASS(); else FAIL("bad");

    TEST("sd.rename -> nil, 'sd: not mounted'");
    if (luaOk(R"(local r,e = sd.rename("/a","/b"); ok = (r==nil and e=="sd: not mounted"))")) PASS(); else FAIL("bad");

    TEST("sd.copy -> nil, 'sd: not mounted'");
    if (luaOk(R"(local r,e = sd.copy("/a","/b"); ok = (r==nil and e=="sd: not mounted"))")) PASS(); else FAIL("bad");

    TEST("sd.remove/mkdir -> nil, 'sd: not mounted'");
    if (luaOk(R"(
        local r1,e1 = sd.remove("/a")
        local r2,e2 = sd.mkdir("/b")
        ok = (r1==nil and e1=="sd: not mounted" and r2==nil and e2=="sd: not mounted")
    )")) PASS(); else FAIL("bad");

    TEST("sd.info -> nil (single return)");
    if (luaOk(R"(local r = sd.info(); ok = (r==nil))")) PASS(); else FAIL("bad");

    eng.shutdown();

    if (g_passed == g_total) printf("\n=== ALL %d SD API TESTS PASSED ===\n", g_total);
    else                     printf("\n=== %d/%d ===\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
