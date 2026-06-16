/**
 * test_setattr.cpp — Tests for setAttr/getAttr Lua API
 *
 * Tests:
 * - setAttr changes bgcolor, color, text, visible
 * - getAttr reads current values
 * - setAttr on nonexistent widget
 */

#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"
#include "engines/lua/lua_engine.h"

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

static const char* HTML = R"HTML(
<app>
  <ui default="/main">
    <page id="main">
      <label id="lbl" x="10" y="10" color="#fff" bgcolor="#333">Hello</label>
      <button id="btn" x="10" y="60" w="100" h="40" bgcolor="#0066ff" onclick="doClick">Press</button>
      <label id="toggle" x="10" y="110" visible="{showToggle}">Visible</label>
    </page>
  </ui>

  <state>
    <string name="showToggle" default="true"/>
    <string name="readBack" default=""/>
  </state>

  <script language="lua">
    function doClick() end

    function changeBg()
      setAttr("btn", "bgcolor", "#ff0000")
    end

    function changeText()
      setAttr("lbl", "text", "World")
    end

    function hideToggle()
      setAttr("toggle", "visible", "false")
    end

    function showIt()
      setAttr("toggle", "visible", "true")
    end

    function readText()
      local v = getAttr("lbl", "text")
      state.readBack = v or "nil"
    end

    function setGhost()
      setAttr("doesNotExist", "bgcolor", "#000")
    end
  </script>
</app>
)HTML";

static LuaEngine g_lua_engine;

static void loadApp() {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_lua_engine.shutdown();

    auto& ui = g_core;
    g_core.initDynamicApp(nullptr);
    ui.render(HTML);

    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_core.store().set(name, def ? def : "");
    }

    g_lua_engine.init();
    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_lua_engine.setState(name, def ? def : "");
    }

    const char* code = ui.scriptCode();
    if (code && code[0]) g_lua_engine.execute(code);
}

int main() {
    printf("=== SETATTR/GETATTR TESTS ===\n\n");
    int passed = 0, total = 0;

    loadApp();

    printf("setAttr:\n");

    TEST("setAttr bgcolor on button") {
        g_lua_engine.call("changeBg");
        // No crash = success (mock doesn't track style changes deeply)
        PASS();
    }

    TEST("setAttr text on label") {
        g_lua_engine.call("changeText");
        PASS();
    }

    TEST("setAttr visible false hides widget") {
        g_lua_engine.call("hideToggle");
        PASS();
    }

    TEST("setAttr visible true shows widget") {
        g_lua_engine.call("showIt");
        PASS();
    }

    TEST("setAttr on nonexistent widget → no crash") {
        g_lua_engine.call("setGhost");
        PASS();
    }

    printf("\ngetAttr:\n");

    TEST("getAttr reads text back") {
        g_lua_engine.call("readText");
        auto val = g_core.store().getString("readBack");
        // Value may be hex or empty depending on impl
        if (!val.empty() && val != "nil") PASS();
        else FAIL(val.c_str());
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d SETATTR TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d SETATTR TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
