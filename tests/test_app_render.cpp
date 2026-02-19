/**
 * Test App Render - Load real app.html through real UI::Engine
 * REAL code creates LVGL widgets → Mock captures → KDL output
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
      <label id="display" x="10" y="10" w="200" h="40">0</label>
      <button x="10" y="60" w="50" h="50" onclick="d7">7</button>
      <button x="70" y="60" w="50" h="50" onclick="d8">8</button>
      <button x="130" y="60" w="50" h="50" onclick="d9">9</button>
      <button x="10" y="120" w="50" h="50" onclick="d4">4</button>
      <button x="70" y="120" w="50" h="50" onclick="d5">5</button>
      <button x="130" y="120" w="50" h="50" onclick="d6">6</button>
      <button x="10" y="180" w="50" h="50" onclick="d1">1</button>
      <button x="70" y="180" w="50" h="50" onclick="d2">2</button>
      <button x="130" y="180" w="50" h="50" onclick="d3">3</button>
      <button x="10" y="240" w="50" h="50" onclick="d0">0</button>
      <button x="70" y="240" w="110" h="50" onclick="eq">=</button>
    </page>
  </ui>
</app>
)";

int main() {
    printf("=== App Render Test ===\n");
    printf("Testing REAL UI::Engine with LVGL mock capture\n\n");
    
    // Create mock screen
    LvglMock::create_screen(480, 480);
    
    // Init state
    State::store().clear();
    
    // Render through REAL UI::Engine (our code!)
    printf("Rendering app...\n");
    int result = UI::Engine::instance().render(CALC_APP);
    printf("Render result: %d widgets\n", result);
    
    // Output KDL
    printf("\n=== KDL Output ===\n%s\n", LvglMock::to_kdl().c_str());
    
    // Verify
    int buttons = LvglMock::count("Button");
    int labels = LvglMock::count("Label");  // Includes button labels!
    int total = LvglMock::g_all.size();
    
    printf("=== Verification ===\n");
    printf("Buttons: %d (expected 11)\n", buttons);
    printf("Labels: %d (includes button text labels)\n", labels);
    printf("Total widgets: %d\n", (int)total);
    
    // Check buttons have correct text
    bool hasDigit7 = LvglMock::find_by_text("7") != nullptr;
    bool hasEquals = LvglMock::find_by_text("=") != nullptr;
    
    printf("Has digit '7': %s\n", hasDigit7 ? "YES" : "NO");
    printf("Has '=': %s\n", hasEquals ? "YES" : "NO");
    
    bool pass = (buttons == 11) && hasDigit7 && hasEquals;
    printf("\n%s\n", pass ? "=== PASS ===" : "=== FAIL ===");
    
    return pass ? 0 : 1;
}
