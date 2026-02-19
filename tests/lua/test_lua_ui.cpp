/**
 * Test: Lua UI Functions
 * focus(), setAttr(), getAttr(), navigate() called from Lua scripts
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"
#include "engines/lua/lua_engine.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static const char* APP_HTML = R"(
<app>
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
      setAttr("lbl1", "text", "Updated")
      setAttr("btn1", "bgcolor", "#ff0000")
      state.result = "setAttr done"
    end
    
    function testSetAttrVisible()
      setAttr("lbl1", "visible", "false")
      state.result = "hidden"
    end
    
    function testNavigate()
      navigate("/second")
    end
    
    function testNavigateBack()
      navigate("/main")
    end
    
    function testSetAttrBadId()
      local ok = setAttr("nonexistent", "text", "X")
      state.result = ok and "true" or "false"
    end
    
    function testFocusExists()
      -- focus() is registered as a global function
      state.result = type(focus) == "function" and "yes" or "no"
    end
    
    function testGetAttrExists()
      state.result = type(getAttr) == "function" and "yes" or "no"
    end
    
    function testNavigateExists()
      state.result = type(navigate) == "function" and "yes" or "no"
    end
  </script>
</app>
)";

int main() {
    printf("=== Lua UI Functions Tests ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(240, 240);
    auto& state = State::store();
    state.clear();

    auto& ui = UI::Engine::instance();
    ui.init();
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

    // === Function registration ===
    printf("Registration:\n");

    TEST("focus() registered as function") {
        engine.call("testFocusExists");
        if (state.getString("result") == "yes") PASS(); else FAIL(state.getString("result").c_str());
    }

    TEST("getAttr() registered as function") {
        engine.call("testGetAttrExists");
        if (state.getString("result") == "yes") PASS(); else FAIL(state.getString("result").c_str());
    }

    TEST("navigate() registered as function") {
        engine.call("testNavigateExists");
        if (state.getString("result") == "yes") PASS(); else FAIL(state.getString("result").c_str());
    }

    // === setAttr ===
    printf("\nsetAttr:\n");

    TEST("setAttr runs without error") {
        engine.call("testSetAttr");
        if (state.getString("result") == "setAttr done") PASS();
        else FAIL(state.getString("result").c_str());
    }

    TEST("setAttr visible=false") {
        engine.call("testSetAttrVisible");
        if (state.getString("result") == "hidden") PASS();
        else FAIL(state.getString("result").c_str());
    }

    TEST("setAttr nonexistent widget returns false") {
        engine.call("testSetAttrBadId");
        if (state.getString("result") == "false") PASS();
        else FAIL(state.getString("result").c_str());
    }

    // === navigate ===
    printf("\nnavigate:\n");

    TEST("navigate to /second") {
        engine.call("testNavigate");
        auto page = ui.currentPageId();
        if (page && strcmp(page, "second") == 0) PASS(); else FAIL(page ? page : "null");
    }

    TEST("navigate back to /main") {
        engine.call("testNavigateBack");
        auto page = ui.currentPageId();
        if (page && strcmp(page, "main") == 0) PASS(); else FAIL(page ? page : "null");
    }

    engine.shutdown();

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d LUA UI TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d LUA UI TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
