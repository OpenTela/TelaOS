/**
 * Test: HTML IDs captured by mock
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* APP = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="title" x="10" y="10">Hello</label>
      <button id="btn_ok" x="10" y="60" w="100" h="40">OK</button>
      <button id="btn_cancel" x="120" y="60" w="100" h="40">Cancel</button>
    </page>
  </ui>
</app>
)";

#define TEST(name) printf("  %-35s ", name)
#define PASS() printf("✓\n")
#define FAIL() do { printf("✗\n"); failures++; } while(0)

int main() {
    printf("=== Test: HTML IDs ===\n\n");
    int failures = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    UI::Engine::instance().render(APP);
    
    auto* page = LvglMock::g_screen->first("Container");
    
    TEST("Page has id 'main'");
    if (page && page->id == "main") PASS(); else FAIL();
    
    TEST("findById('title') works");
    auto* title = page->findById("title", true);
    if (title && title->id == "title") PASS(); else FAIL();
    
    TEST("findById('btn_ok') works");
    auto* btnOk = page->findById("btn_ok", false);
    if (btnOk && btnOk->id == "btn_ok") PASS(); else FAIL();
    
    TEST("findById('btn_cancel') works");
    auto* btnCancel = page->findById("btn_cancel", false);
    if (btnCancel && btnCancel->id == "btn_cancel") PASS(); else FAIL();
    
    TEST("btn_ok is a Button");
    if (btnOk && btnOk->type == "Button") PASS(); else FAIL();
    
    TEST("btn_ok contains 'OK' text");
    if (btnOk && btnOk->findByText("OK", true)) PASS(); else FAIL();
    
    TEST("btn_cancel at x=120");
    if (btnCancel && btnCancel->x == 120) PASS(); else FAIL();
    
    printf("\n%s\n", failures == 0 ? "=== ALL PASSED ===" : "=== FAILED ===");
    return failures;
}
