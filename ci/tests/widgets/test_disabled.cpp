/**
 * Test: disabled attribute
 *
 * Tests:
 * - disabled="{var}" initial state from store (true and false)
 * - static disabled="true"
 * - runtime toggle via ui_update_bindings
 * - non-bool values treated as enabled
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"

extern void ui_update_bindings(const char* varname, const char* value);

const char* APP = R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <button id="dynOff" x="10" y="10"  w="100" h="40" disabled="{lockA}">A</button>
      <button id="dynOn"  x="10" y="60"  w="100" h="40" disabled="{lockB}">B</button>
      <button id="statOn" x="10" y="110" w="100" h="40" disabled="true">C</button>
      <button id="plain"  x="10" y="160" w="100" h="40">D</button>
      <slider id="sldOn"  x="10" y="210" w="200" h="20" disabled="{lockB}"/>
    </page>
  </ui>

  <state>
    <string name="lockA" default="false"/>
    <string name="lockB" default="true"/>
  </state>
</app>
)";

#define TEST(name) printf("  %-50s ", name);
#define PASS() printf("\u2713\n")
#define FAIL(msg) do { printf("\u2717 %s\n", msg); failures++; } while(0)

int main() {
    printf("=== Disabled Attribute Tests ===\n\n");
    int failures = 0;

    LvglMock::create_screen(480, 480);
    g_core.store().clear();

    int count = g_core.render(APP);
    printf("Rendered %d widgets\n\n", count);

    auto* page = LvglMock::g_screen->first("Container");
    if (!page) { printf("FATAL: no page\n"); return 1; }

    TEST("dynamic {lockA}=false -> enabled");
    { auto* w = page->findById("dynOff");
      if (w && !w->disabled) PASS(); else FAIL("expected enabled"); }

    TEST("dynamic {lockB}=true -> disabled");
    { auto* w = page->findById("dynOn");
      if (w && w->disabled) PASS(); else FAIL("expected disabled"); }

    TEST("static disabled=\"true\" -> disabled");
    { auto* w = page->findById("statOn");
      if (w && w->disabled) PASS(); else FAIL("expected disabled"); }

    TEST("no attribute -> enabled");
    { auto* w = page->findById("plain");
      if (w && !w->disabled) PASS(); else FAIL("expected enabled"); }

    TEST("slider follows {lockB}=true");
    { auto* w = page->findById("sldOn");
      if (w && w->disabled) PASS(); else FAIL("expected disabled"); }

    // Runtime toggles
    ui_update_bindings("lockA", "true");
    TEST("runtime lockA=true -> becomes disabled");
    { auto* w = page->findById("dynOff");
      if (w && w->disabled) PASS(); else FAIL("expected disabled"); }

    ui_update_bindings("lockB", "false");
    TEST("runtime lockB=false -> button re-enabled");
    { auto* w = page->findById("dynOn");
      if (w && !w->disabled) PASS(); else FAIL("expected enabled"); }

    TEST("runtime lockB=false -> slider re-enabled");
    { auto* w = page->findById("sldOn");
      if (w && !w->disabled) PASS(); else FAIL("expected enabled"); }

    ui_update_bindings("lockA", "banana");
    TEST("non-bool value -> treated as enabled");
    { auto* w = page->findById("dynOff");
      if (w && !w->disabled) PASS(); else FAIL("expected enabled"); }

    if (failures == 0) printf("\n=== ALL 9 DISABLED TESTS PASSED ===\n");
    else               printf("\n=== %d FAILURES ===\n", failures);
    return failures == 0 ? 0 : 1;
}
