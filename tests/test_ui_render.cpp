/**
 * Test UI Render - Basic rendering test
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* SIMPLE_APP = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="title" x="10" y="10">Hello World</label>
      <button id="btn" x="10" y="50" w="100" h="40">Click</button>
    </page>
  </ui>
</app>
)";

int main() {
    printf("=== UI Render Test ===\n\n");
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(SIMPLE_APP);
    printf("Rendered %d widgets\n", count);
    
    // Verify
    auto* page = LvglMock::g_screen->first("Container");
    if (!page) {
        printf("FAIL: No page container\n");
        return 1;
    }
    
    auto* title = page->findById("title");
    auto* btn = page->findById("btn");
    
    printf("title: %s\n", title ? title->text.c_str() : "NOT FOUND");
    printf("btn: %s\n", btn ? "FOUND" : "NOT FOUND");
    
    if (title && title->text == "Hello World" && btn) {
        printf("\n=== PASS ===\n");
        return 0;
    } else {
        printf("\n=== FAIL ===\n");
        return 1;
    }
}
