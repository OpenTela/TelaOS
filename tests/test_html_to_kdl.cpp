/**
 * Test HTML to KDL - Verify KDL output format
 */
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* APP = R"(
<app>
  <ui default="/main">
    <page id="main" bgcolor="#222">
      <label id="lbl" x="10" y="10" color="#ff0000">Red Text</label>
      <button id="btn" x="10" y="50" w="100" h="40" bgcolor="#0066ff">Blue Button</button>
      <slider id="sld" x="10" y="100" w="200" min="0" max="100" bind="val"/>
    </page>
  </ui>
  <state>
    <int name="val" default="50"/>
  </state>
</app>
)";

int main() {
    printf("=== HTML to KDL Test ===\n\n");
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(APP);
    printf("Rendered %d widgets\n\n", count);
    
    std::string kdl = LvglMock::to_kdl();
    printf("KDL output (%zu bytes):\n%s\n", kdl.size(), kdl.c_str());
    
    // Verify KDL contains expected elements
    int checks = 0;
    int passed = 0;
    
    #define CHECK(name, cond) do { \
        checks++; \
        if (cond) { printf("  %s: OK\n", name); passed++; } \
        else { printf("  %s: FAIL\n", name); } \
    } while(0)
    
    printf("\nVerifying KDL content:\n");
    CHECK("Has Screen", kdl.find("Screen") != std::string::npos);
    CHECK("Has Container", kdl.find("Container") != std::string::npos);
    CHECK("Has Label", kdl.find("Label") != std::string::npos);
    CHECK("Has Button", kdl.find("Button") != std::string::npos);
    CHECK("Has Slider", kdl.find("Slider") != std::string::npos);
    CHECK("Has id \"lbl\"", kdl.find("id \"lbl\"") != std::string::npos);
    CHECK("Has id \"btn\"", kdl.find("id \"btn\"") != std::string::npos);
    CHECK("Has text \"Red Text\"", kdl.find("\"Red Text\"") != std::string::npos);
    CHECK("Has width 100", kdl.find("width 100") != std::string::npos);
    CHECK("Has range", kdl.find("range") != std::string::npos);
    
    printf("\n%d/%d checks passed\n", passed, checks);
    
    if (passed == checks) {
        printf("\n=== ALL PASSED ===\n");
        return 0;
    } else {
        printf("\n=== FAILED ===\n");
        return 1;
    }
}
