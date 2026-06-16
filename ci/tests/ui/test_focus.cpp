/**
 * test_focus.cpp — Tests for focus() / UI::focusInput() API
 *
 * Tests:
 * - focus on existing input → true, textarea tracked
 * - focus on button (not input) → false
 * - focus on nonexistent widget → false
 * - focus from Lua → sets focused textarea
 * - focus switches between inputs
 * - getFocusedTextarea returns correct object
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

static const char* HTML = R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <button id="btn1" x="10" y="10" w="100" h="40" onclick="doNothing">Click</button>
      <input id="inp1" x="10" y="60" w="200" h="35" bind="val1" placeholder="First"/>
      <input id="inp2" x="10" y="110" w="200" h="35" bind="val2" placeholder="Second"/>
      <label id="lbl1" x="10" y="160">Just a label</label>
    </page>
  </ui>

  <state>
    <string name="val1" default=""/>
    <string name="val2" default=""/>
    <string name="focusOk" default=""/>
    <string name="focusBtnOk" default=""/>
    <string name="focusGhostOk" default=""/>
  </state>

  <script language="lua">
    function doNothing() end

    function focusFirst()
      local ok = focus("inp1")
      if ok then state.focusOk = "yes" else state.focusOk = "no" end
    end

    function focusSecond()
      focus("inp2")
    end

    function focusButton()
      local ok = focus("btn1")
      if ok then state.focusBtnOk = "yes" else state.focusBtnOk = "no" end
    end

    function focusGhost()
      local ok = focus("doesNotExist")
      if ok then state.focusGhostOk = "yes" else state.focusGhostOk = "no" end
    end
  </script>
</app>
)";

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
    printf("=== FOCUS API TESTS ===\n\n");
    int passed = 0, total = 0;

    loadApp();

    // --- Direct C++ API ---
    printf("Direct API (focusInput):\n");

    TEST("focus on input → returns true") {
        bool ok = UI::focusInput("inp1");
        if (ok) PASS();
        else FAIL("returned false");
    }

    TEST("focus on input → getFocusedTextarea not null") {
        lv_obj_t* ta = getFocusedTextarea();
        if (ta != nullptr) PASS();
        else FAIL("null");
    }

    TEST("focus on button → returns false") {
        bool ok = UI::focusInput("btn1");
        if (!ok) PASS();
        else FAIL("should return false for non-input");
    }

    TEST("focus on label → returns false") {
        bool ok = UI::focusInput("lbl1");
        if (!ok) PASS();
        else FAIL("should return false for label");
    }

    TEST("focus on nonexistent → returns false") {
        bool ok = UI::focusInput("doesNotExist");
        if (!ok) PASS();
        else FAIL("should return false for missing widget");
    }

    TEST("focus switches to inp2 → getFocusedTextarea changes") {
        UI::focusInput("inp1");
        lv_obj_t* ta1 = getFocusedTextarea();
        UI::focusInput("inp2");
        lv_obj_t* ta2 = getFocusedTextarea();
        if (ta1 && ta2 && ta1 != ta2) PASS();
        else FAIL("textarea should change");
    }

    TEST("focus inp1 again → back to first textarea") {
        UI::focusInput("inp2");
        lv_obj_t* ta2 = getFocusedTextarea();
        UI::focusInput("inp1");
        lv_obj_t* ta1 = getFocusedTextarea();
        if (ta1 && ta2 && ta1 != ta2) PASS();
        else FAIL("should switch back");
    }

    // --- Lua API ---
    printf("\nLua API (focus()):\n");

    TEST("Lua focus('inp1') → returns true via state") {
        g_lua_engine.call("focusFirst");
        if (g_core.store().getString("focusOk") == "yes") PASS();
        else FAIL(g_core.store().getString("focusOk").c_str());
    }

    TEST("Lua focus('btn1') → returns false via state") {
        g_lua_engine.call("focusButton");
        if (g_core.store().getString("focusBtnOk") == "no") PASS();
        else FAIL("should return false for button");
    }

    TEST("Lua focus('doesNotExist') → returns false via state") {
        g_lua_engine.call("focusGhost");
        if (g_core.store().getString("focusGhostOk") == "no") PASS();
        else FAIL("should return false for missing");
    }

    TEST("Lua focus sets getFocusedTextarea") {
        g_lua_engine.call("focusSecond");
        lv_obj_t* ta = getFocusedTextarea();
        if (ta != nullptr) PASS();
        else FAIL("textarea should be set after Lua focus");
    }

    // --- Summary ---
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d FOCUS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d FOCUS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
