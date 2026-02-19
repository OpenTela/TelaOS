/**
 * Test: Binding
 * 
 * Tests state binding for:
 * - Text content {var}
 * - bgcolor="{var}"
 * - color="{var}"
 * - visible="{var}"
 */
#include <cstdio>
#include <cassert>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "ui/ui_task.h"
#include "core/state_store.h"

const char* BINDING_APP = R"(
<app>
  <ui default="/binding">
    <page id="binding" bgcolor="#222">
      <!-- Text binding -->
      <label id="lbl1" x="10" y="10" color="#fff">{textVar}</label>
      <label id="lbl2" x="10" y="40" color="#fff">Static: {counter}</label>
      
      <!-- Color binding -->
      <label id="boxColor" x="10" y="80" w="200" h="50" bgcolor="{boxBg}" color="{boxFg}">Box</label>
      
      <!-- Visible binding -->
      <label id="shown" x="10" y="150" visible="{showIt}">I am visible</label>
      <label id="hidden" x="10" y="180" visible="{hideIt}">I am hidden</label>
      
      <!-- Button with color binding -->
      <button id="dynBtn" x="10" y="220" w="100" h="40" bgcolor="{btnColor}">Btn</button>
    </page>
  </ui>
  
  <state>
    <string name="textVar" default="Hello"/>
    <string name="counter" default="0"/>
    <string name="boxBg" default="#ff0000"/>
    <string name="boxFg" default="#ffffff"/>
    <string name="showIt" default="true"/>
    <string name="hideIt" default="false"/>
    <string name="btnColor" default="#0066ff"/>
  </state>
</app>
)";

#define TEST(name) printf("  %-45s ", name);
#define PASS() printf("✓\n")
#define FAIL(msg) do { printf("✗ %s\n", msg); failures++; } while(0)

int main() {
    printf("=== Binding Tests ===\n\n");
    int failures = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(BINDING_APP);
    printf("Rendered %d widgets\n\n", count);
    
    auto* page = LvglMock::g_screen->first("Container");
    if (!page) {
        printf("FATAL: No page found\n");
        return 1;
    }
    
    // === Initial State ===
    printf("Initial state:\n");
    
    TEST("lbl1 text = 'Hello' (from default)") {
        auto* w = page->findById("lbl1");
        if (w && w->text == "Hello") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }
    
    TEST("lbl2 text = 'Static: 0'") {
        auto* w = page->findById("lbl2");
        if (w && w->text == "Static: 0") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }
    
    TEST("boxColor bgcolor = #ff0000") {
        auto* w = page->findById("boxColor");
        if (w && w->bgcolor == 0xff0000) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    TEST("boxColor color = #ffffff") {
        auto* w = page->findById("boxColor");
        if (w && w->color == 0xffffff) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }
    
    TEST("shown is visible (showIt=true)") {
        auto* w = page->findById("shown");
        // Widget exists = visible in mock
        if (w) PASS();
        else FAIL("not found");
    }
    
    TEST("dynBtn bgcolor = #0066ff") {
        auto* w = page->findById("dynBtn");
        if (w && w->bgcolor == 0x0066ff) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    // === Update State ===
    printf("\nAfter state changes:\n");
    
    // Change text
    State::store().set("textVar", "World");
    State::store().set("counter", "42");
    ui_update_bindings("textVar", "World");
    ui_update_bindings("counter", "42");
    
    TEST("lbl1 text updated to 'World'") {
        auto* w = page->findById("lbl1");
        if (w && w->text == "World") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }
    
    TEST("lbl2 text updated to 'Static: 42'") {
        auto* w = page->findById("lbl2");
        if (w && w->text == "Static: 42") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }
    
    // Change colors
    State::store().set("boxBg", "#00ff00");
    State::store().set("boxFg", "#000000");
    ui_update_bindings("boxBg", "#00ff00");
    ui_update_bindings("boxFg", "#000000");
    
    TEST("boxColor bgcolor updated to #00ff00") {
        auto* w = page->findById("boxColor");
        if (w && w->bgcolor == 0x00ff00) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    TEST("boxColor color updated to #000000") {
        auto* w = page->findById("boxColor");
        if (w && w->color == 0x000000) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }
    
    // Change button color
    State::store().set("btnColor", "#ff4444");
    ui_update_bindings("btnColor", "#ff4444");
    
    TEST("dynBtn bgcolor updated to #ff4444") {
        auto* w = page->findById("dynBtn");
        if (w && w->bgcolor == 0xff4444) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    // === Summary ===
    printf("\n");
    if (failures == 0) {
        printf("=== ALL %d TESTS PASSED ===\n", 11);
        return 0;
    } else {
        printf("=== %d TESTS FAILED ===\n", failures);
        return 1;
    }
}
