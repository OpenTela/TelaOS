/**
 * test_escaping.cpp — Tests for quoted literal text escaping
 *
 * Content in double quotes = literal, no binding.
 * "" inside = escaped quote.
 */

#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

static const char* HTML = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <label id="litPlain">"{count} items"</label>
      <label id="litQuotes">"She said ""hello"""</label>
      <label id="litBraces">"JSON: {""key"": ""val""}"</label>
      <label id="dynBind">{count} items</label>
      <label id="noquote">no quotes here</label>
    </page>
  </ui>
  <state>
    <string name="count" default="5"/>
  </state>
  <script language="lua"></script>
</app>
)HTML";

int main() {
    printf("=== ESCAPING TESTS ===\n\n");
    int passed = 0, total = 0;

    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(HTML);

    auto* page = LvglMock::g_screen->first("Container");

    TEST("literal: \"{count} items\" shows as-is") {
        auto* w = page->findById("litPlain");
        if (w && w->text == "{count} items") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }

    TEST("literal: escaped quotes \"\" -> \"") {
        auto* w = page->findById("litQuotes");
        if (w && w->text == "She said \"hello\"") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }

    TEST("literal: braces + escaped quotes") {
        auto* w = page->findById("litBraces");
        if (w && w->text == "JSON: {\"key\": \"val\"}") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }

    TEST("dynamic: {count} binds normally") {
        auto* w = page->findById("dynBind");
        if (w && w->text == "5 items") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }

    TEST("no quotes: plain text unchanged") {
        auto* w = page->findById("noquote");
        if (w && w->text == "no quotes here") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d ESCAPING TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d ESCAPING TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
