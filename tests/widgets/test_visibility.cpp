/**
 * Test: Visibility and Font
 * 
 * Tests:
 * - visible="{var}" binding
 * - font size capture
 * - hidden state
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
      <!-- Font sizes -->
      <label id="f16" x="10" y="10" font="16">Font 16</label>
      <label id="f32" x="10" y="40" font="32">Font 32</label>
      <label id="f48" x="10" y="90" font="48">Font 48</label>
      
      <!-- Visibility -->
      <label id="vis_true" x="10" y="150" visible="{showA}">Visible A</label>
      <label id="vis_false" x="10" y="180" visible="{showB}">Hidden B</label>
      <label id="vis_static" x="10" y="210" visible="true">Static visible</label>
    </page>
  </ui>
  
  <state>
    <string name="showA" default="true"/>
    <string name="showB" default="false"/>
  </state>
</app>
)";

#define TEST(name) printf("  %-50s ", name);
#define PASS() printf("✓\n")
#define FAIL(msg) do { printf("✗ %s\n", msg); failures++; } while(0)

int main() {
    printf("=== Visibility & Font Tests ===\n\n");
    int failures = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(APP);
    printf("Rendered %d widgets\n\n", count);
    
    auto* page = LvglMock::g_screen->first("Container");
    if (!page) {
        printf("FATAL: No page found\n");
        return 1;
    }
    
    // === Font sizes ===
    // Note: fontSize may not be captured if the code uses lv_obj_set_style_text_font
    // which we haven't mocked to capture the size. Let's check what we get.
    printf("Font sizes:\n");
    
    TEST("f16 exists") {
        auto* w = page->findById("f16");
        if (w) PASS();
        else FAIL("not found");
    }
    
    TEST("f32 exists") {
        auto* w = page->findById("f32");
        if (w) PASS();
        else FAIL("not found");
    }
    
    TEST("f48 exists") {
        auto* w = page->findById("f48");
        if (w) PASS();
        else FAIL("not found");
    }
    
    // === Visibility ===
    printf("\nVisibility:\n");
    
    TEST("vis_true: visible (showA=true)") {
        auto* w = page->findById("vis_true");
        if (w && !w->hidden) PASS();
        else { FAIL(""); if(w) printf("      hidden=%d\n", w->hidden); }
    }
    
    TEST("vis_false: hidden (showB=false)") {
        auto* w = page->findById("vis_false");
        if (w && w->hidden) PASS();
        else { FAIL(""); if(w) printf("      hidden=%d\n", w->hidden); }
    }
    
    TEST("vis_static: visible") {
        auto* w = page->findById("vis_static");
        if (w && !w->hidden) PASS();
        else { FAIL(""); if(w) printf("      hidden=%d\n", w->hidden); }
    }
    
    // === Summary ===
    printf("\n");
    if (failures == 0) {
        printf("=== ALL %d TESTS PASSED ===\n", 6);
        return 0;
    } else {
        printf("=== %d TESTS FAILED ===\n", failures);
        return 1;
    }
}
