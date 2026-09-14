/**
 * test_templates.cpp — Tests for <templates>/<template> and @for directives
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

static const char* HTML_TEMPLATE_BASIC = R"HTML(
<app os="1.0">
  <templates>
    <template id="Btn">
      <button id="btn_{n}" onclick="tap">{label_{n}}</button>
    </template>
  </templates>

  <ui default="/main">
    <page id="main">
      <Btn n="0"/>
      <Btn n="1"/>
      <Btn n="2"/>
    </page>
  </ui>

  <state>
    <string name="label_0" default="Zero"/>
    <string name="label_1" default="One"/>
    <string name="label_2" default="Two"/>
  </state>
  <script language="lua">
    function tap() end
  </script>
</app>
)HTML";

static const char* HTML_FOR_BASIC = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      @for(i in 0..4) {
        <button id="b_{i}" onclick="tap({i})">{label_{i}}</button>
      }
    </page>
  </ui>

  <state>
    <string name="label_0" default="A"/>
    <string name="label_1" default="B"/>
    <string name="label_2" default="C"/>
    <string name="label_3" default="D"/>
    <string name="label_4" default="E"/>
    <string name="result" default=""/>
  </state>
  <script language="lua">
    function tap(i)
      state.result = tostring(i)
    end
  </script>
</app>
)HTML";

static const char* HTML_FOR_NESTED = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <table w="100%">
        @for(r in 0..2) {
          <tr>
            @for(c in 0..2) {
              <td><button id="c_{r}_{c}">{val_{r}_{c}}</button></td>
            }
          </tr>
        }
      </table>
    </page>
  </ui>

  <state>
    <string name="val_0_0" default="00"/>
    <string name="val_0_1" default="01"/>
    <string name="val_0_2" default="02"/>
    <string name="val_1_0" default="10"/>
    <string name="val_1_1" default="11"/>
    <string name="val_1_2" default="12"/>
    <string name="val_2_0" default="20"/>
    <string name="val_2_1" default="21"/>
    <string name="val_2_2" default="22"/>
  </state>
  <script language="lua"></script>
</app>
)HTML";

static const char* HTML_FOR_TEMPLATE = R"HTML(
<app os="1.0">
  <templates>
    <template id="Cell">
      <button id="c_{r}_{c}" bgcolor="{bg_{r}_{c}}" onclick="tap({r},{c})">{val_{r}_{c}}</button>
    </template>
  </templates>

  <ui default="/main">
    <page id="main">
      <table w="100%">
        @for(r in 0..1) {
          <tr>
            @for(c in 0..1) {
              <td><Cell r="{r}" c="{c}"/></td>
            }
          </tr>
        }
      </table>
    </page>
  </ui>

  <state>
    <string name="val_0_0" default="A1"/>
    <string name="val_0_1" default="B1"/>
    <string name="val_1_0" default="A2"/>
    <string name="val_1_1" default="B2"/>
    <string name="bg_0_0" default="#333"/>
    <string name="bg_0_1" default="#444"/>
    <string name="bg_1_0" default="#444"/>
    <string name="bg_1_1" default="#333"/>
    <string name="tapped" default=""/>
  </state>
  <script language="lua">
    function tap(r, c)
      state.tapped = r .. "_" .. c
    end
  </script>
</app>
)HTML";

static const char* HTML_FOR_STEP = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      @for(i in 0..10 step 5) {
        <label id="s_{i}">{i}</label>
      }
    </page>
  </ui>
  <state/>
  <script language="lua"></script>
</app>
)HTML";

static const char* HTML_CONFLICT = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      @for(count in 0..2) {
        <button id="x_{count}">X</button>
      }
    </page>
  </ui>
  <state>
    <int name="count" default="0"/>
  </state>
  <script language="lua"></script>
</app>
)HTML";

static const char* HTML_SLASH_IN_ATTR = R"HTML(
<app os="1.0">
  <templates>
    <template id="Op">
      <button id="op_{id}" onclick="{click}">{label}</button>
    </template>
  </templates>
  <ui default="/main">
    <page id="main">
      <Op id="div" click="opDiv" label="/"/>
      <Op id="mul" click="opMul" label="*"/>
    </page>
  </ui>
  <state/>
  <script language="lua">
    function opDiv() end
    function opMul() end
  </script>
</app>
)HTML";

static LuaEngine g_lua_engine;

static void loadApp(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_lua_engine.shutdown();

    auto& ui = g_core;
    g_core.initDynamicApp(nullptr);
    ui.render(html);

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

static void renderOnly(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    // removed: g_app = ::g_core.initDynamicApp(nullptr);
    ::g_core.render(html);
}

int main() {
    printf("=== TEMPLATE & @FOR TESTS ===\n\n");
    int passed = 0, total = 0;

    printf("Template direct invocation:\n");
    renderOnly(HTML_TEMPLATE_BASIC);
    auto* page = LvglMock::g_screen->first("Container");

    TEST("btn_0 exists") {
        if (page && page->findById("btn_0")) PASS();
        else FAIL("not found");
    }

    TEST("btn_1 exists") {
        if (page && page->findById("btn_1")) PASS();
        else FAIL("not found");
    }

    TEST("btn_2 exists") {
        if (page && page->findById("btn_2")) PASS();
        else FAIL("not found");
    }

    TEST("3 buttons from 3 template calls") {
        int count = page ? page->count("Button", true) : 0;
        if (count == 3) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 3, got %d", count); FAIL(buf); }
    }

    printf("\n@for basic (5 buttons):\n");
    renderOnly(HTML_FOR_BASIC);
    page = LvglMock::g_screen->first("Container");

    TEST("5 buttons created") {
        int count = page ? page->count("Button", true) : 0;
        if (count == 5) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 5, got %d", count); FAIL(buf); }
    }

    TEST("b_0 exists") {
        if (page && page->findById("b_0")) PASS();
        else FAIL("not found");
    }

    TEST("b_4 exists") {
        if (page && page->findById("b_4")) PASS();
        else FAIL("not found");
    }

    printf("\n@for nested 3x3 grid:\n");
    renderOnly(HTML_FOR_NESTED);
    page = LvglMock::g_screen->first("Container");

    TEST("9 buttons in 3x3 grid") {
        int count = page ? page->count("Button", true) : 0;
        if (count == 9) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 9, got %d", count); FAIL(buf); }
    }

    TEST("c_0_0 exists") {
        if (page && page->findById("c_0_0")) PASS();
        else FAIL("not found");
    }

    TEST("c_1_2 exists") {
        if (page && page->findById("c_1_2")) PASS();
        else FAIL("not found");
    }

    TEST("c_2_2 exists (last cell)") {
        if (page && page->findById("c_2_2")) PASS();
        else FAIL("not found");
    }

    printf("\n@for + template (2x2):\n");
    loadApp(HTML_FOR_TEMPLATE);
    page = LvglMock::g_screen->first("Container");

    TEST("4 buttons from 2x2 loop + template") {
        int count = page ? page->count("Button", true) : 0;
        if (count == 4) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 4, got %d", count); FAIL(buf); }
    }

    TEST("c_0_0 exists") {
        if (page && page->findById("c_0_0")) PASS();
        else FAIL("not found");
    }

    TEST("c_1_1 exists") {
        if (page && page->findById("c_1_1")) PASS();
        else FAIL("not found");
    }

    TEST("onclick tap(0,1) sets state.tapped") {
        g_lua_engine.execute("tap(0,1)");
        auto val = g_core.store().getString("tapped");
        if (val == "0_1") PASS();
        else FAIL(val.c_str());
    }

    TEST("onclick tap(1,0) sets state.tapped") {
        g_lua_engine.execute("tap(1,0)");
        auto val = g_core.store().getString("tapped");
        if (val == "1_0") PASS();
        else FAIL(val.c_str());
    }

    printf("\n@for with step:\n");
    renderOnly(HTML_FOR_STEP);
    page = LvglMock::g_screen->first("Container");

    TEST("3 labels from step 5 (0,5,10)") {
        int count = page ? page->count("Label", true) : 0;
        if (count == 3) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 3, got %d", count); FAIL(buf); }
    }

    TEST("s_0 exists") {
        if (page && page->findById("s_0")) PASS();
        else FAIL("not found");
    }

    TEST("s_5 exists") {
        if (page && page->findById("s_5")) PASS();
        else FAIL("not found");
    }

    TEST("s_10 exists") {
        if (page && page->findById("s_10")) PASS();
        else FAIL("not found");
    }

    printf("\nState conflict detection:\n");
    renderOnly(HTML_CONFLICT);
    page = LvglMock::g_screen->first("Container");

    TEST("conflict: no buttons created (var 'count' in state)") {
        int count = page ? page->count("Button", true) : 0;
        if (count == 0) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 0 (conflict), got %d", count); FAIL(buf); }
    }

    printf("\nSlash in attribute value:\n");
    renderOnly(HTML_SLASH_IN_ATTR);
    page = LvglMock::g_screen->first("Container");

    TEST("op_div exists (label='/')") {
        if (page && page->findById("op_div")) PASS();
        else FAIL("not found");
    }

    TEST("op_mul exists (label='*')") {
        if (page && page->findById("op_mul")) PASS();
        else FAIL("not found");
    }

    TEST("2 buttons total") {
        int count = page ? page->count("Button", true) : 0;
        if (count == 2) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 2, got %d", count); FAIL(buf); }
    }



    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TEMPLATE TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d TEMPLATE TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
