/**
 * Test: Input Widgets
 * 
 * Tests LVGL call capture for:
 * - Slider (range, value)
 * - Switch (checked state)
 * - Input/TextArea (placeholder, password)
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* WIDGETS_APP = R"(
<app>
  <ui default="/main">
    <page id="main" bgcolor="#222">
      <!-- Slider -->
      <slider id="vol" x="10" y="20" w="200" min="0" max="100" bind="volume"/>
      <slider id="bright" x="10" y="70" w="200" min="0" max="255" bind="brightness"/>
      
      <!-- Switch -->
      <switch id="sw_on" x="10" y="120" bind="enabled"/>
      <switch id="sw_off" x="120" y="120" bind="disabled"/>
      
      <!-- Input -->
      <input id="name" x="10" y="170" w="200" h="40" bind="userName" placeholder="Enter name"/>
      <input id="pass" x="10" y="220" w="200" h="40" bind="password" placeholder="Password" password="true"/>
    </page>
  </ui>
  
  <state>
    <int name="volume" default="50"/>
    <int name="brightness" default="128"/>
    <bool name="enabled" default="true"/>
    <bool name="disabled" default="false"/>
    <string name="userName" default=""/>
    <string name="password" default=""/>
  </state>
</app>
)";

#define TEST(name) printf("  %-50s ", name);
#define PASS() printf("✓\n")
#define FAIL(msg) do { printf("✗ %s\n", msg); failures++; } while(0)

int main() {
    printf("=== Input Widgets Tests ===\n\n");
    int failures = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(WIDGETS_APP);
    printf("Rendered %d widgets\n\n", count);
    
    auto* page = LvglMock::g_screen->first("Container");
    if (!page) {
        printf("FATAL: No page found\n");
        return 1;
    }
    
    // === Sliders ===
    printf("Sliders:\n");
    
    TEST("vol slider: range 0-100") {
        auto* w = page->findById("vol");
        if (w && w->minValue == 0 && w->maxValue == 100) PASS();
        else { FAIL(""); if(w) printf("      got min=%d max=%d\n", w->minValue, w->maxValue); }
    }
    
    TEST("vol slider: value 50 (from state)") {
        auto* w = page->findById("vol");
        if (w && w->curValue == 50) PASS();
        else { FAIL(""); if(w) printf("      got value=%d\n", w->curValue); }
    }
    
    TEST("bright slider: range 0-255") {
        auto* w = page->findById("bright");
        if (w && w->minValue == 0 && w->maxValue == 255) PASS();
        else { FAIL(""); if(w) printf("      got min=%d max=%d\n", w->minValue, w->maxValue); }
    }
    
    TEST("bright slider: value 128 (from state)") {
        auto* w = page->findById("bright");
        if (w && w->curValue == 128) PASS();
        else { FAIL(""); if(w) printf("      got value=%d\n", w->curValue); }
    }
    
    // === Switches ===
    printf("\nSwitches:\n");
    
    TEST("sw_on checked (enabled=true)") {
        auto* w = page->findById("sw_on");
        if (w && w->checked) PASS();
        else { FAIL(""); if(w) printf("      got checked=%d\n", w->checked); }
    }
    
    TEST("sw_off not checked (disabled=false)") {
        auto* w = page->findById("sw_off");
        if (w && !w->checked) PASS();
        else { FAIL(""); if(w) printf("      got checked=%d\n", w->checked); }
    }
    
    // === Inputs ===
    printf("\nInputs:\n");
    
    TEST("name input: placeholder 'Enter name'") {
        auto* w = page->findById("name");
        if (w && w->placeholder == "Enter name") PASS();
        else { FAIL(""); if(w) printf("      got '%s'\n", w->placeholder.c_str()); }
    }
    
    TEST("name input: not password mode") {
        auto* w = page->findById("name");
        if (w && !w->password) PASS();
        else { FAIL(""); if(w) printf("      got password=%d\n", w->password); }
    }
    
    TEST("pass input: placeholder 'Password'") {
        auto* w = page->findById("pass");
        if (w && w->placeholder == "Password") PASS();
        else { FAIL(""); if(w) printf("      got '%s'\n", w->placeholder.c_str()); }
    }
    
    TEST("pass input: password mode enabled") {
        auto* w = page->findById("pass");
        if (w && w->password) PASS();
        else { FAIL(""); if(w) printf("      got password=%d\n", w->password); }
    }
    
    // === Summary ===
    printf("\n");
    if (failures == 0) {
        printf("=== ALL %d TESTS PASSED ===\n", 10);
        return 0;
    } else {
        printf("=== %d TESTS FAILED ===\n", failures);
        return 1;
    }
}
