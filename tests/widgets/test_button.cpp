/**
 * test_button.cpp — Tests for button widget
 *
 * Tests:
 * - triggerClick fires onclick handler
 * - button with dynamic text/bgcolor binding
 * - onclick with arguments through triggerClick pipeline
 * - multiple clicks update state correctly
 */

#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"
#include "core/call_queue.h"
#include "engines/lua/lua_engine.h"

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

static const char* HTML = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <button id="btnSimple" x="10" y="10" w="100" h="40" onclick="onSimple">Click</button>
      <button id="btnCount" x="10" y="60" w="100" h="40" onclick="onCount">+1</button>
      <button id="btnDyn" x="10" y="110" w="100" h="40" onclick="onDyn" bgcolor="{btnColor}">{btnText}</button>
      <button id="btnArgs" x="10" y="160" w="100" h="40" onclick="onArgs(42,7)">Args</button>
      <button id="btnArgs2" x="10" y="210" w="100" h="40" onclick="onArgs(1,2)">A2</button>
    </page>
  </ui>

  <state>
    <string name="result" default=""/>
    <string name="counter" default="0"/>
    <string name="btnColor" default="#333"/>
    <string name="btnText" default="Dynamic"/>
    <string name="argResult" default=""/>
  </state>

  <script language="lua">
    function onSimple()
      state.result = "clicked"
    end

    function onCount()
      local n = tonumber(state.counter) or 0
      state.counter = tostring(n + 1)
    end

    function onDyn()
      state.result = "dyn"
    end

    function onArgs(a, b)
      state.argResult = tostring(a) .. "_" .. tostring(b)
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
    CallQueue::shutdown();
    CallQueue::init();

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

    CallQueue::setHandler([](const P::String& funcName) {
        if (funcName.find('(') != P::String::npos) {
            g_lua_engine.execute(funcName.c_str());
        } else {
            g_lua_engine.call(funcName.c_str());
        }
    });

    ui.setOnClickHandler([](const char* fn) {
        if (fn && fn[0]) CallQueue::push(fn);
    });

    const char* code = ui.scriptCode();
    if (code && code[0]) g_lua_engine.execute(code);
}

int main() {
    printf("=== BUTTON TESTS ===\n\n");
    int passed = 0, total = 0;

    loadApp();

    printf("Basic onclick:\n");

    TEST("btnSimple exists") {
        auto* page = LvglMock::g_screen->first("Container");
        if (page && page->findById("btnSimple")) PASS();
        else FAIL("not found");
    }

    TEST("triggerClick → state.result = 'clicked'") {
        triggerClick("btnSimple");
        CallQueue::process();
        auto val = g_core.store().getString("result");
        if (val == "clicked") PASS();
        else FAIL(val.c_str());
    }

    printf("\nMultiple clicks:\n");

    TEST("first click → counter = 1") {
        triggerClick("btnCount");
        CallQueue::process();
        auto val = g_core.store().getString("counter");
        if (val == "1") PASS();
        else FAIL(val.c_str());
    }

    TEST("second click → counter = 2") {
        triggerClick("btnCount");
        CallQueue::process();
        auto val = g_core.store().getString("counter");
        if (val == "2") PASS();
        else FAIL(val.c_str());
    }

    TEST("third click → counter = 3") {
        triggerClick("btnCount");
        CallQueue::process();
        auto val = g_core.store().getString("counter");
        if (val == "3") PASS();
        else FAIL(val.c_str());
    }

    printf("\nDynamic binding:\n");

    TEST("btnDyn text = 'Dynamic' (from state)") {
        auto* page = LvglMock::g_screen->first("Container");
        auto* w = page ? page->findById("btnDyn") : nullptr;
        bool found = false;
        if (w) {
            for (auto* child : w->children) {
                if (child->text == "Dynamic") { found = true; break; }
            }
        }
        if (found) PASS();
        else FAIL(w ? "wrong text" : "not found");
    }

    printf("\nOnclick with arguments:\n");

    TEST("btnArgs → onArgs(42,7) → '42_7'") {
        g_core.store().set("argResult", "");
        triggerClick("btnArgs");
        CallQueue::process();
        auto val = g_core.store().getString("argResult");
        if (val == "42_7") PASS();
        else FAIL(val.c_str());
    }

    TEST("btnArgs2 → onArgs(1,2) → '1_2'") {
        g_core.store().set("argResult", "");
        triggerClick("btnArgs2");
        CallQueue::process();
        auto val = g_core.store().getString("argResult");
        if (val == "1_2") PASS();
        else FAIL(val.c_str());
    }

    TEST("different buttons → different args") {
        g_core.store().set("argResult", "");
        triggerClick("btnArgs");
        CallQueue::process();
        auto v1 = g_core.store().getString("argResult");
        g_core.store().set("argResult", "");
        triggerClick("btnArgs2");
        CallQueue::process();
        auto v2 = g_core.store().getString("argResult");
        if (v1 == "42_7" && v2 == "1_2") PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "v1=%s v2=%s", v1.c_str(), v2.c_str()); FAIL(buf); }
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d BUTTON TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d BUTTON TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
