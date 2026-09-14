/**
 * Test: flow-style YAML arrays in <state> (os="2.0")
 *
 * `row0: ["", "", ""]` used to be stored as one scalar STRING — the checkers
 * app rendered binding names on the emulator and a black screen on device.
 * Now flow-style brackets parse as arrays, same as block-style "- item".
 */
#include <cstdio>
#include <cstring>
#include "ui/ui_html.h"
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"

static int passed = 0, total = 0;
#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("\u2713\n"); passed++; } while(0)
#define FAIL(msg) do { printf("\u2717 %s\n", msg); } while(0)

int main() {
    printf("=== YAML Flow-Style Array Tests ===\n\n");

    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(R"(
<app os="2.0">
  <ui default="/main">
    <page id="main">
      <label id="c00">{row0[0]}</label>
    </page>
  </ui>
  <state>
turnLabel: "White"
row0: ["", "", "", "", "", ""]
mixed: [alpha, "beta, still beta", 'gamma']
nums: [1, 2, 3]
empty: []
block:
  - one
  - two
nested:
  inner: [x, y]
  </state>
</app>
)");
    auto& st = g_core.store();

    TEST("checkers row0 is an array of 6");
    if (st.hasArray("row0") && st.getArraySize("row0") == 6) PASS();
    else { char m[64]; snprintf(m,64,"hasArray=%d size=%d",(int)st.hasArray("row0"),st.getArraySize("row0")); FAIL(m); }

    TEST("row0 items are empty strings");
    if (st.getArrayItem("row0", 0) == "" && st.getArrayItem("row0", 5) == "") PASS();
    else FAIL("non-empty");

    TEST("row0 is NOT a scalar string");
    if (!st.has("row0")) PASS(); else FAIL("stored as scalar too");

    TEST("bare + quoted items, comma inside quotes");
    if (st.getArraySize("mixed") == 3 &&
        st.getArrayItem("mixed", 0) == "alpha" &&
        st.getArrayItem("mixed", 1) == "beta, still beta" &&
        st.getArrayItem("mixed", 2) == "gamma") PASS();
    else { char m[96]; snprintf(m,96,"n=%d '%s'|'%s'|'%s'", st.getArraySize("mixed"),
        st.getArrayItem("mixed",0).c_str(), st.getArrayItem("mixed",1).c_str(),
        st.getArrayItem("mixed",2).c_str()); FAIL(m); }

    TEST("numeric items kept as strings");
    if (st.getArraySize("nums") == 3 && st.getArrayItem("nums", 1) == "2") PASS();
    else FAIL("bad");

    TEST("empty brackets -> empty array");
    if (st.hasArray("empty") && st.getArraySize("empty") == 0) PASS();
    else FAIL("bad");

    TEST("block-style still works");
    if (st.getArraySize("block") == 2 && st.getArrayItem("block", 0) == "one") PASS();
    else FAIL("regressed");

    TEST("scalar with quotes unaffected");
    if (st.getString("turnLabel") == "White") PASS(); else FAIL("bad");

    TEST("flow array under nested prefix");
    if (st.getArraySize("nested.inner") == 2 && st.getArrayItem("nested.inner", 1) == "y") PASS();
    else { char m[64]; snprintf(m,64,"n=%d",st.getArraySize("nested.inner")); FAIL(m); }

    TEST("binding {row0[0]} renders empty, not literal");
    // resolve via ui_update path: label got initial render from the array
    // (empty string), so its text must not contain "row0".
    auto* page = LvglMock::g_screen->first("Container");
    auto* w = page ? page->findById("c00") : nullptr;
    if (w && w->text.find("row0") == std::string::npos) PASS();
    else FAIL(w ? w->text.c_str() : "(no widget)");

    if (passed == total) printf("\n=== ALL %d YAML FLOW TESTS PASSED ===\n", total);
    else                 printf("\n=== %d/%d ===\n", passed, total);
    return passed == total ? 0 : 1;
}
