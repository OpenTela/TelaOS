#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"
#include "engines/lua/lua_engine.h"

static const char* HTML = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <label id="lbl" x="10" y="10">test</label>
    </page>
  </ui>
  <state>
    <int name="month" default="3"/>
    <string name="result" default=""/>
  </state>
  <script language="lua">
  </script>
</app>
)HTML";

int main() {
    printf("=== STATE TYPE COERCION TEST ===\n\n");
    int failures = 0;

    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();

    auto& ui = g_core;
    g_core.initDynamicApp(nullptr);
    ui.render(HTML);

    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_core.store().set(name, def ? def : "");
    }

    LuaEngine engine;
    engine.init();
    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) engine.setState(name, def ? def : "");
    }

    const char* code = ui.scriptCode();
    if (code && code[0]) engine.execute(code);

    // Test 1: default int is number in Lua
    printf("  [1] default <int>=3 type in Lua:  ");
    engine.execute("state.result = type(state.month)");
    auto r = g_core.store().getString("result");
    if (r == "number") printf("PASS (%s)\n", r.c_str());
    else { printf("FAIL: got '%s'\n", r.c_str()); failures++; }

    // Test 2: default int == 3 comparison
    printf("  [2] state.month == 3:             ");
    engine.execute("if state.month == 3 then state.result = 'yes' else state.result = 'no:' .. type(state.month) .. ':' .. tostring(state.month) end");
    r = g_core.store().getString("result");
    if (r == "yes") printf("PASS\n");
    else { printf("FAIL: got '%s'\n", r.c_str()); failures++; }

    // Test 3: write STRING to <int>, then read type
    printf("  [3] after state.month='5', type:  ");
    engine.execute("state.month = '5'");
    engine.execute("state.result = type(state.month)");
    r = g_core.store().getString("result");
    if (r == "number") printf("PASS (%s)\n", r.c_str());
    else { printf("FAIL: got '%s' — ENGINE BUG!\n", r.c_str()); failures++; }

    // Test 4: after string write, == comparison
    printf("  [4] state.month == 5 after str:   ");
    engine.execute("if state.month == 5 then state.result = 'yes' else state.result = 'no:' .. type(state.month) .. ':' .. tostring(state.month) end");
    r = g_core.store().getString("result");
    if (r == "yes") printf("PASS\n");
    else { printf("FAIL: got '%s' — ENGINE BUG!\n", r.c_str()); failures++; }

    // Test 5: write number, verify
    printf("  [5] after state.month=7, type:    ");
    engine.execute("state.month = 7");
    engine.execute("state.result = type(state.month)");
    r = g_core.store().getString("result");
    if (r == "number") printf("PASS (%s)\n", r.c_str());
    else { printf("FAIL: got '%s'\n", r.c_str()); failures++; }

    // Test 6: write via setState(key, "9") from C - simulates UI binding update
    printf("  [6] after C setState('9'), type:  ");
    engine.setState("month", "9");
    engine.execute("state.result = type(state.month)");
    r = g_core.store().getString("result");
    if (r == "number") printf("PASS (%s)\n", r.c_str());
    else { printf("FAIL: got '%s' — ENGINE BUG!\n", r.c_str()); failures++; }

    // Test 7: after C setState, == comparison
    printf("  [7] state.month == 9 after C set: ");
    engine.execute("if state.month == 9 then state.result = 'yes' else state.result = 'no:' .. type(state.month) .. ':' .. tostring(state.month) end");
    r = g_core.store().getString("result");
    if (r == "yes") printf("PASS\n");
    else { printf("FAIL: got '%s' — ENGINE BUG!\n", r.c_str()); failures++; }

    printf("\n");
    if (failures == 0) {
        printf("=== ALL TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== %d TESTS FAILED ===\n", failures);
        return 1;
    }
}
