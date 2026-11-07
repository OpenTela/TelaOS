/**
 * E2E: Files app boots in mock (no SD) and its Lua flows work.
 * Loads the real data/apps/files/files.bax from the TelaOS tree.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"

static int g_passed = 0, g_total = 0;
#define TEST(name) printf("  %-55s ", name); g_total++;
#define PASS() do { printf("\u2713\n"); g_passed++; } while(0)
#define FAIL(msg) printf("\u2717 %s\n", msg)

static LuaEngine g_lua;

static std::string get(const char* var) { return std::string(g_core.store().getString(var).c_str()); }

int main() {
    printf("=== Files App E2E (mock, no SD) ===\n\n");

    std::ifstream f(std::string(TELAOS_ROOT) + "/data/apps/files/files.bax");
    if (!f) { printf("FATAL: files.bax not found\n"); return 1; }
    std::stringstream ss; ss << f.rdbuf();
    std::string appHtml = ss.str();

    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();

    auto& ui = g_core;
    g_core.initDynamicApp(nullptr);
    int n = ui.render(appHtml.c_str());
    printf("Rendered %d widgets\n\n", n);

    // Seed defaults into store + engine (crossword harness pattern)
    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_core.store().set(name, def ? def : "");
    }
    g_lua.init();
    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_lua.setState(name, def ? def : "");
    }
    const char* code = ui.scriptCode();
    bool scriptOk = code && code[0] && g_lua.execute(code);

    TEST("app renders (>10 widgets)");
    if (n > 10) PASS(); else FAIL("few widgets");

    TEST("top-level script executes");
    if (scriptOk) PASS(); else FAIL("script error");

    TEST("boot: status = 'No SD'");
    if (get("status") == "No SD") PASS(); else FAIL(get("status").c_str());

    TEST("boot: cwd '/' and atRoot true");
    if (get("cwd") == "/" && g_core.store().getBool("atRoot")) PASS(); else FAIL("bad");

    TEST("entries is an empty array");
    if (g_core.store().hasArray("entries") &&
        g_core.store().getArraySize("entries") == 0) PASS(); else FAIL("bad");

    TEST("newFolder() opens prompt");
    g_lua.execute("newFolder()");
    if (get("promptTitle") == "New folder") PASS(); else FAIL(get("promptTitle").c_str());

    TEST("promptOk() empty -> 'Empty name'");
    g_lua.execute("promptOk()");
    if (get("promptError") == "Empty name") PASS(); else FAIL(get("promptError").c_str());

    TEST("promptOk() with slash -> 'No slashes'");
    g_lua.execute("state.promptValue = 'a/b'");
    g_lua.execute("promptOk()");
    if (get("promptError") == "No slashes") PASS(); else FAIL(get("promptError").c_str());

    TEST("promptCancel() clears");
    g_lua.execute("promptCancel()");
    if (get("promptError") == "" && get("promptValue") == "") PASS(); else FAIL("bad");

    TEST("onPick on empty list: no crash");
    if (g_lua.execute("onPick(1, 'x')")) PASS(); else FAIL("errored");

    TEST("goUp at root: no-op");
    g_lua.execute("goUp()");
    if (get("cwd") == "/") PASS(); else FAIL(get("cwd").c_str());

    TEST("prompt flow ok with valid name (mkdir fails gracefully, no SD)");
    g_lua.execute("newFolder()");
    g_lua.execute("state.promptValue = 'docs'");
    if (g_lua.execute("promptOk()") && get("status") != "") PASS(); else FAIL("bad");

    if (g_passed == g_total) printf("\n=== ALL %d FILES APP TESTS PASSED ===\n", g_total);
    else                     printf("\n=== %d/%d ===\n", g_passed, g_total);
    return g_passed == g_total ? 0 : 1;
}
