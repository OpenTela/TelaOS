/**
 * Test: Lua UI Functions (new API)
 * ui.navigate, ui.setAttr, ui.getAttr, ui.focus, ui.freeze, ui.unfreeze
 * Alias: navigate()
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"
#include "engines/lua/lua_engine.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static const char* APP_HTML = R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <label id="lbl1" x="10" y="10" color="#ffffff">Hello</label>
      <button id="btn1" x="10" y="50" w="100" h="40" bgcolor="#0066ff" onclick="onBtn">Click</button>
      <input id="inp1" x="10" y="100" w="200" h="40" bind="inputVal" placeholder="Type..."/>
    </page>
    <page id="second">
      <label id="lbl2" x="10" y="10">Page 2</label>
    </page>
  </ui>
  
  <state>
    <string name="inputVal" default=""/>
    <string name="result" default=""/>
  </state>
  
  <script language="lua">
    function testSetAttr()
      ui.setAttr("lbl1", "text", "Updated")
      ui.setAttr("btn1", "bgcolor", "#ff0000")
      state.result = "setAttr done"
    end
    
    function testSetAttrVisible()
      ui.setAttr("lbl1", "visible", "false")
      state.result = "hidden"
    end
    
    function testNavigate()
      ui.navigate("/second")
    end
    
    function testNavigateAlias()
      navigate("/main")
    end
    
    function testSetAttrBadId()
      local ok = ui.setAttr("nonexistent", "text", "X")
      state.result = ok and "true" or "false"
    end
    
    function testNamespaceExists()
      state.result = type(ui) == "table" and "yes" or "no"
    end
    
    function testFunctions()
      local all = type(ui.navigate) == "function"
        and type(ui.setAttr) == "function"
        and type(ui.getAttr) == "function"
        and type(ui.focus) == "function"
        and type(ui.freeze) == "function"
        and type(ui.unfreeze) == "function"
      state.result = all and "yes" or "no"
    end
    
    function testNavigateAliasFn()
      state.result = type(navigate) == "function" and "yes" or "no"
    end
    
    function testFreeze()
      ui.freeze()
      ui.unfreeze()
      state.result = "ok"
    end
  </script>
</app>
)";

int main() {
    printf("=== Lua UI Tests (new API) ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(240, 240);
    auto& state = g_core.store();
    state.clear();

    auto& ui = g_core;
    g_core.initDynamicApp(nullptr);
    ui.render(APP_HTML);

    LuaEngine engine;
    engine.init();

    for (size_t i = 0; i < state.count(); i++) {
        auto name = state.nameAt(i);
        engine.setState(name.c_str(), state.getAsString(name).c_str());
    }

    const char* scriptStart = strstr(APP_HTML, "<script");
    const char* codeStart = scriptStart ? strstr(scriptStart, ">") : nullptr;
    const char* codeEnd = scriptStart ? strstr(scriptStart, "</script>") : nullptr;
    if (codeStart && codeEnd) {
        codeStart++;
        std::string code(codeStart, codeEnd - codeStart);
        engine.execute(code.c_str());
    }

    // === Namespace ===
    printf("Namespace:\n");

    TEST("ui is a table") {
        engine.call("testNamespaceExists");
        if (state.getString("result") == "yes") PASS(); else FAIL(state.getString("result").c_str());
    }

    TEST("all ui.* functions exist") {
        engine.call("testFunctions");
        if (state.getString("result") == "yes") PASS(); else FAIL(state.getString("result").c_str());
    }

    TEST("navigate() alias exists") {
        engine.call("testNavigateAliasFn");
        if (state.getString("result") == "yes") PASS(); else FAIL(state.getString("result").c_str());
    }

    // === ui.setAttr ===
    printf("\nui.setAttr:\n");

    TEST("ui.setAttr runs without error") {
        engine.call("testSetAttr");
        if (state.getString("result") == "setAttr done") PASS();
        else FAIL(state.getString("result").c_str());
    }

    TEST("ui.setAttr visible=false") {
        engine.call("testSetAttrVisible");
        if (state.getString("result") == "hidden") PASS();
        else FAIL(state.getString("result").c_str());
    }

    TEST("ui.setAttr nonexistent returns false") {
        engine.call("testSetAttrBadId");
        if (state.getString("result") == "false") PASS();
        else FAIL(state.getString("result").c_str());
    }

    // === ui.navigate ===
    printf("\nui.navigate:\n");

    TEST("ui.navigate to /second") {
        engine.call("testNavigate");
        auto page = ui.currentPageId();
        if (page && strcmp(page, "second") == 0) PASS(); else FAIL(page ? page : "null");
    }

    TEST("navigate() alias to /main") {
        engine.call("testNavigateAlias");
        auto page = ui.currentPageId();
        if (page && strcmp(page, "main") == 0) PASS(); else FAIL(page ? page : "null");
    }

    // === ui.freeze / ui.unfreeze ===
    printf("\nui.freeze:\n");

    TEST("ui.freeze + ui.unfreeze no crash") {
        engine.call("testFreeze");
        if (state.getString("result") == "ok") PASS();
        else FAIL(state.getString("result").c_str());
    }

    engine.shutdown();

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d LUA UI TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d LUA UI TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
