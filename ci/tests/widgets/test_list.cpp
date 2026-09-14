/**
 * Test: <list> widget
 *
 * - <list> creates a container; static <item> children become buttons
 * - bind="arr" fills items from a state array (<array> XML tag, bracketed default)
 * - full-array replace (setArray + bare-name update) rebuilds the list
 * - per-index update (arr[N]) patches one button's text
 * - empty array -> empty list
 */
#include <cstdio>
#include <cstring>
#include "ui/ui_html.h"
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"

extern void ui_update_bindings(const char* varname, const char* value);

static int passed = 0, total = 0;
#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("\u2713\n"); passed++; } while(0)
#define FAIL(msg) do { printf("\u2717 %s\n", msg); } while(0)

static int find_list() {
    // Search from the end: each render appends elements, and earlier ones
    // reference mock objects wiped by LvglMock::reset().
    for (int i = (int)g_core.app().elements.size() - 1; i >= 0; i--) {
        if (g_core.app().elements[i] && g_core.app().elements[i]->is_list) return i;
    }
    return -1;
}

static lv_obj_t* list_obj(int idx) {
    return g_core.app().elements[idx]->w.handle;
}

int main() {
    printf("=== List Widget Tests ===\n\n");

    // ---- Static items ----
    {
        LvglMock::reset();
        LvglMock::create_screen(480, 480);
        g_core.store().clear();
        g_core.render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <list id="menu">
        <item>One</item>
        <item onclick="doTwo">Two</item>
        <item>Three</item>
      </list>
    </page>
  </ui>
</app>
)");
        int li = find_list();
        TEST("static: list created");
        if (li >= 0) PASS(); else FAIL("no list element");

        if (li >= 0) {
            TEST("static: three items");
            if (lv_obj_get_child_cnt(list_obj(li)) == 3) PASS();
            else FAIL("wrong child count");

            TEST("static: first item text");
            lv_obj_t* b0 = lv_obj_get_child(list_obj(li), 0);
            const char* t = b0 ? lv_list_get_button_text(list_obj(li), b0) : "";
            if (t && strcmp(t, "One") == 0) PASS(); else FAIL(t ? t : "(null)");
        }
    }

    // ---- Dynamic bind ----
    {
        LvglMock::reset();
        LvglMock::create_screen(480, 480);
        g_core.store().clear();
        g_core.render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <list id="files" bind="files" itemClick="onTap"/>
    </page>
  </ui>
  <state>
    <array name="files" default="['a.txt','b.txt','c.txt']"/>
  </state>
</app>
)");
        int li = find_list();
        TEST("dynamic: list created");
        if (li >= 0) PASS(); else FAIL("no list element");

        if (li >= 0) {
            lv_obj_t* lst = list_obj(li);

            TEST("dynamic: three items from bracketed default");
            if (lv_obj_get_child_cnt(lst) == 3) PASS();
            else { char m[48]; snprintf(m,48,"got %d",(int)lv_obj_get_child_cnt(lst)); FAIL(m); }

            TEST("dynamic: first item text matches");
            lv_obj_t* b0 = lv_obj_get_child(lst, 0);
            const char* t = b0 ? lv_list_get_button_text(lst, b0) : "";
            if (t && strcmp(t, "a.txt") == 0) PASS();
            else { char m[64]; snprintf(m,64,"got '%s'", t?t:"(null)"); FAIL(m); }

            // Full replace
            P::Array<P::String> ni;
            ni.push_back("x"); ni.push_back("y");
            g_core.store().setArray("files", ni, false);
            ui_update_bindings("files", "");

            TEST("dynamic: full replace rebuilds (3 -> 2)");
            if (lv_obj_get_child_cnt(lst) == 2) PASS();
            else { char m[48]; snprintf(m,48,"got %d",(int)lv_obj_get_child_cnt(lst)); FAIL(m); }

            // Per-index patch
            g_core.store().setArrayItem("files", 1, "Z", false);
            ui_update_bindings("files[1]", "Z");

            TEST("dynamic: per-index patch");
            lv_obj_t* b1 = lv_obj_get_child(lst, 1);
            const char* t1 = b1 ? lv_list_get_button_text(lst, b1) : "";
            if (t1 && strcmp(t1, "Z") == 0) PASS();
            else { char m[64]; snprintf(m,64,"got '%s'", t1?t1:"(null)"); FAIL(m); }

            TEST("dynamic: per-index did not change count");
            if (lv_obj_get_child_cnt(lst) == 2) PASS(); else FAIL("count changed");
        }
    }

    // ---- Empty array ----
    {
        LvglMock::reset();
        LvglMock::create_screen(480, 480);
        g_core.store().clear();
        g_core.render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <list id="x" bind="items"/>
    </page>
  </ui>
  <state>
    <array name="items"/>
  </state>
</app>
)");
        int li = find_list();
        TEST("empty array: list created, zero children");
        if (li >= 0 && lv_obj_get_child_cnt(list_obj(li)) == 0) PASS();
        else FAIL("bad");
    }

    if (passed == total) printf("\n=== ALL %d LIST TESTS PASSED ===\n", total);
    else                 printf("\n=== %d/%d ===\n", passed, total);
    return passed == total ? 0 : 1;
}
