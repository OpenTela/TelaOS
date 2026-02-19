/**
 * UI Test: Styles - bgcolor, color, radius, font
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
      <!-- Labels with colors -->
      <label id="red_text" x="10" y="10" color="#ff0000">Red Text</label>
      <label id="blue_bg" x="10" y="50" bgcolor="#0000ff">Blue Background</label>
      <label id="styled" x="10" y="100" w="200" h="50" color="#00ff00" bgcolor="#333333" radius="12">Styled</label>
      
      <!-- Buttons -->
      <button id="btn_blue" x="10" y="170" w="100" h="40" bgcolor="#0066ff">Blue</button>
      <button id="btn_red" x="120" y="170" w="100" h="40" bgcolor="#ff4444">Red</button>
    </page>
  </ui>
</app>
)";

#define TEST(name) printf("  %-45s ", name)
#define PASS() printf("✓\n")
#define FAIL(msg) do { printf("✗ %s\n", msg); failures++; } while(0)

int main() {
    printf("=== UI Styles Tests ===\n\n");
    int failures = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int n = UI::Engine::instance().render(APP);
    printf("Rendered %d widgets\n", n);
    
    // Debug: show KDL
    printf("\nKDL:\n%s\n", LvglMock::to_kdl().c_str());
    
    auto* page = LvglMock::g_screen->first("Container");
    if (!page) { printf("FATAL: No page\n"); return 1; }
    
    // === Structure ===
    printf("Structure:\n");
    
    TEST("Has 3 labels");
    if (page->count("Label", false) == 3) PASS(); else FAIL("");
    
    TEST("Has 2 buttons");
    if (page->count("Button", false) == 2) PASS(); else FAIL("");
    
    // === Colors ===
    printf("\nColors:\n");
    
    TEST("red_text has color #ff0000");
    auto* red = page->findById("red_text", false);
    if (red && red->hasColor && red->color == 0xff0000) PASS();
    else { FAIL(""); if(red) printf("      color=0x%06x hasColor=%d\n", red->color, red->hasColor); }
    
    TEST("blue_bg has bgcolor #0000ff");
    auto* blue = page->findById("blue_bg", false);
    if (blue && blue->hasBgcolor && blue->bgcolor == 0x0000ff) PASS();
    else { FAIL(""); if(blue) printf("      bgcolor=0x%06x hasBgcolor=%d\n", blue->bgcolor, blue->hasBgcolor); }
    
    TEST("styled has color #00ff00");
    auto* styled = page->findById("styled", false);
    if (styled && styled->hasColor && styled->color == 0x00ff00) PASS();
    else { FAIL(""); if(styled) printf("      color=0x%06x\n", styled->color); }
    
    TEST("styled has bgcolor #333333");
    if (styled && styled->hasBgcolor && styled->bgcolor == 0x333333) PASS();
    else { FAIL(""); if(styled) printf("      bgcolor=0x%06x\n", styled->bgcolor); }
    
    TEST("styled has radius 12");
    if (styled && styled->radius == 12) PASS();
    else { FAIL(""); if(styled) printf("      radius=%d\n", styled->radius); }
    
    // === Buttons ===
    printf("\nButtons:\n");
    
    TEST("btn_blue has bgcolor #0066ff");
    auto* btnBlue = page->findById("btn_blue", false);
    if (btnBlue && btnBlue->hasBgcolor && btnBlue->bgcolor == 0x0066ff) PASS();
    else { FAIL(""); if(btnBlue) printf("      bgcolor=0x%06x\n", btnBlue->bgcolor); }
    
    TEST("btn_red has bgcolor #ff4444");
    auto* btnRed = page->findById("btn_red", false);
    if (btnRed && btnRed->hasBgcolor && btnRed->bgcolor == 0xff4444) PASS();
    else { FAIL(""); if(btnRed) printf("      bgcolor=0x%06x\n", btnRed->bgcolor); }
    
    // === Summary ===
    printf("\n%s\n", failures == 0 ? "=== ALL PASSED ===" : "=== FAILED ===");
    return failures;
}
