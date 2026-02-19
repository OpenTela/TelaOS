/**
 * Test HTML Parser - Verify HTML parsing and widget creation
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* CALC_APP = R"(
<app>
  <ui default="/calc">
    <page id="calc">
      <label id="display" x="10" y="10" w="200" h="40">{display}</label>
      <button id="b7" x="10" y="60" w="40" h="40" onclick="d7">7</button>
      <button id="b8" x="60" y="60" w="40" h="40" onclick="d8">8</button>
      <button id="b9" x="110" y="60" w="40" h="40" onclick="d9">9</button>
      <button id="bDiv" x="160" y="60" w="40" h="40" onclick="opDiv">/</button>
      <button id="b4" x="10" y="110" w="40" h="40" onclick="d4">4</button>
      <button id="b5" x="60" y="110" w="40" h="40" onclick="d5">5</button>
      <button id="b6" x="110" y="110" w="40" h="40" onclick="d6">6</button>
      <button id="bMul" x="160" y="110" w="40" h="40" onclick="opMul">*</button>
      <button id="b1" x="10" y="160" w="40" h="40" onclick="d1">1</button>
      <button id="b2" x="60" y="160" w="40" h="40" onclick="d2">2</button>
      <button id="b3" x="110" y="160" w="40" h="40" onclick="d3">3</button>
      <button id="bSub" x="160" y="160" w="40" h="40" onclick="opSub">-</button>
      <button id="b0" x="10" y="210" w="40" h="40" onclick="d0">0</button>
      <button id="bC" x="60" y="210" w="40" h="40" onclick="clear">C</button>
      <button id="bEq" x="110" y="210" w="40" h="40" onclick="eq">=</button>
      <button id="bAdd" x="160" y="210" w="40" h="40" onclick="opAdd">+</button>
    </page>
  </ui>
  <state>
    <string name="display" default="0"/>
  </state>
</app>
)";

int main() {
    printf("=== HTML Parser Test ===\n\n");
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(CALC_APP);
    printf("Rendered %d widgets\n\n", count);
    
    auto* page = LvglMock::g_screen->first("Container");
    if (!page) {
        printf("FAIL: No page\n");
        return 1;
    }
    
    int buttons = page->count("Button", false);
    int labels = page->count("Label", false);
    
    printf("Buttons: %d (expected 16)\n", buttons);
    printf("Labels: %d (expected 1 + button labels)\n", labels);
    
    // Check specific buttons
    bool hasDigits = true;
    for (int i = 0; i <= 9; i++) {
        char id[4];
        snprintf(id, sizeof(id), "b%d", i);
        if (!page->findById(id)) {
            printf("Missing button: %s\n", id);
            hasDigits = false;
        }
    }
    
    bool hasOps = page->findById("bAdd") && page->findById("bSub") && 
                  page->findById("bMul") && page->findById("bDiv");
    
    bool hasDisplay = page->findById("display") != nullptr;
    
    printf("\nDigits 0-9: %s\n", hasDigits ? "OK" : "MISSING");
    printf("Operators: %s\n", hasOps ? "OK" : "MISSING");
    printf("Display: %s\n", hasDisplay ? "OK" : "MISSING");
    
    if (buttons == 16 && hasDigits && hasOps && hasDisplay) {
        printf("\n=== ALL PASSED ===\n");
        return 0;
    } else {
        printf("\n=== FAILED ===\n");
        return 1;
    }
}
