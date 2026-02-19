/**
 * UI Test: Calculator App
 */
#include <cstdio>
#include <cassert>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"



const char* CALC_APP = R"(
<app>
  <ui default="/calc">
    <page id="calc">
      <label id="display" x="5%" y="5%" w="90%" h="50" font="48" 
             text-align="right center" bgcolor="#222">0</label>
      
      <button x="5%"  y="80"  w="22%" h="50" onclick="d7">7</button>
      <button x="28%" y="80"  w="22%" h="50" onclick="d8">8</button>
      <button x="51%" y="80"  w="22%" h="50" onclick="d9">9</button>
      <button x="74%" y="80"  w="22%" h="50" onclick="div" bgcolor="#f90">÷</button>
      
      <button x="5%"  y="140" w="22%" h="50" onclick="d4">4</button>
      <button x="28%" y="140" w="22%" h="50" onclick="d5">5</button>
      <button x="51%" y="140" w="22%" h="50" onclick="d6">6</button>
      <button x="74%" y="140" w="22%" h="50" onclick="mul" bgcolor="#f90">×</button>
      
      <button x="5%"  y="200" w="22%" h="50" onclick="d1">1</button>
      <button x="28%" y="200" w="22%" h="50" onclick="d2">2</button>
      <button x="51%" y="200" w="22%" h="50" onclick="d3">3</button>
      <button x="74%" y="200" w="22%" h="50" onclick="sub" bgcolor="#f90">−</button>
      
      <button x="5%"  y="260" w="22%" h="50" onclick="clear" bgcolor="#f44">C</button>
      <button x="28%" y="260" w="22%" h="50" onclick="d0">0</button>
      <button x="51%" y="260" w="22%" h="50" onclick="eq" bgcolor="#4a4">=</button>
      <button x="74%" y="260" w="22%" h="50" onclick="add" bgcolor="#f90">+</button>
    </page>
  </ui>
  
  <state>
    <string name="display" default="0"/>
  </state>
</app>
)";

#define TEST(name) printf("  %-40s ", name); 
#define PASS() printf("✓\n")
#define FAIL(msg) do { printf("✗ %s\n", msg); failures++; } while(0)

// Find display label (direct Label child of page, not inside Button)
LvglMock::Widget* findDisplay(LvglMock::Widget* page) {
    for (auto* child : page->children) {
        if (child->type == "Label") return child;  // first direct Label
    }
    return nullptr;
}

int main() {
    printf("=== Calculator UI Tests ===\n\n");
    int failures = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int result = UI::Engine::instance().render(CALC_APP);
    printf("Rendered %d widgets\n\n", result);
    
    // Debug: print widget tree
    printf("Widget tree:\n%s\n", LvglMock::to_kdl().c_str());
    
    LvglMock::Widget* page = LvglMock::g_screen->first("Container");
    if (!page) {
        // Try TileView/Tile for swipe pages
        page = LvglMock::g_screen->first("Tile");
        if (!page) page = LvglMock::g_screen->first("TileView");
    }
    if (!page) {
        printf("FATAL: No page found!\n");
        printf("Available children types: ");
        for (auto* ch : LvglMock::g_screen->children) {
            printf("%s ", ch->type.c_str());
        }
        printf("\n");
        return 1;
    }
    
    // === Structure Tests ===
    printf("Structure:\n");
    
    TEST("Page has exactly 1 display label") {
        int labels = page->count("Label", false);
        if (labels == 1) PASS();
        else { FAIL(""); printf("    got %d\n", labels); }
    }
    
    TEST("Page has 16 buttons") {
        int buttons = page->count("Button", false);
        if (buttons == 16) PASS();
        else { FAIL(""); printf("    got %d\n", buttons); }
    }
    
    TEST("Total labels (including inside buttons) = 17") {
        int total = page->count("Label", true);
        if (total == 17) PASS();
        else { FAIL(""); printf("    got %d\n", total); }
    }
    
    // === Content Tests ===
    printf("\nContent:\n");
    
    TEST("Display shows '0'") {
        LvglMock::Widget* display = findDisplay(page);
        if (display && display->text == "0") PASS();
        else FAIL("display not found or wrong text");
    }
    
    TEST("Has digit buttons 0-9") {
        bool ok = true;
        for (char c = '0'; c <= '9'; c++) {
            char s[2] = {c, 0};
            if (!page->findByText(s, true)) ok = false;
        }
        if (ok) PASS();
        else FAIL("missing digit");
    }
    
    TEST("Has operator buttons +-×÷=C") {
        bool ok = true;
        const char* ops[] = {"+", "−", "×", "÷", "=", "C"};
        for (auto op : ops) {
            if (!page->findByText(op, true)) ok = false;
        }
        if (ok) PASS();
        else FAIL("");
    }
    
    // === Layout Tests ===
    printf("\nLayout:\n");
    
    TEST("Display at top (y <= 24)") {
        LvglMock::Widget* display = findDisplay(page);
        if (display && display->y <= 24) PASS();
        else { FAIL(""); if(display) printf("    y=%d\n", display->y); }
    }
    
    TEST("All buttons below display (y >= 80)") {
        auto buttons = page->findAll("Button", false);
        bool ok = true;
        for (auto* btn : buttons) {
            if (btn->y < 80) ok = false;
        }
        if (ok) PASS();
        else FAIL("button above y=80");
    }
    
    TEST("Button '7' at row 1 (y=80)") {
        LvglMock::Widget* lbl = page->findByText("7", true);
        LvglMock::Widget* btn = lbl ? lbl->parent : nullptr;
        if (btn && btn->y == 80) PASS();
        else { FAIL(""); if(btn) printf("    y=%d\n", btn->y); }
    }
    
    TEST("Button '=' at row 4 (y=260)") {
        LvglMock::Widget* lbl = page->findByText("=", true);
        LvglMock::Widget* btn = lbl ? lbl->parent : nullptr;
        if (btn && btn->y == 260) PASS();
        else { FAIL(""); if(btn) printf("    y=%d\n", btn->y); }
    }
    
    TEST("Buttons in 4 columns (x: 24, ~134, ~245, ~355)") {
        auto buttons = page->findAll("Button", false);
        int cols[4] = {0, 0, 0, 0};
        for (auto* btn : buttons) {
            if (btn->x < 50) cols[0]++;
            else if (btn->x < 180) cols[1]++;
            else if (btn->x < 300) cols[2]++;
            else cols[3]++;
        }
        // Each column should have 4 buttons
        if (cols[0]==4 && cols[1]==4 && cols[2]==4 && cols[3]==4) PASS();
        else { FAIL(""); printf("    cols: %d %d %d %d\n", cols[0],cols[1],cols[2],cols[3]); }
    }
    
    // === Summary ===
    printf("\n");
    if (failures == 0) {
        printf("=== ALL TESTS PASSED ===\n");
        return 0;
    } else {
        printf("=== %d TESTS FAILED ===\n", failures);
        return 1;
    }
}
