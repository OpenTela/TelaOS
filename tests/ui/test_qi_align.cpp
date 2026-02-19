/**
 * Test: Alignment via QI (KDL)
 * 
 * Tests alignment using UIQuery over KDL representation.
 * This tests the full pipeline: HTML → LVGL mock → KDL → UIQuery
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui_query.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* ALIGN_APP = R"(
<app>
  <ui default="/main">
    <page id="main" bgcolor="#111">
      <!-- Horizontal text-align -->
      <label id="ta_left" x="10" y="10" w="100" h="50" text-align="left">left</label>
      <label id="ta_center" x="120" y="10" w="100" h="50" text-align="center">center</label>
      <label id="ta_right" x="230" y="10" w="100" h="50" text-align="right">right</label>
      
      <!-- Vertical text-align (via pad-top) -->
      <label id="tv_top" x="10" y="70" w="100" h="70" text-valign="top">top</label>
      <label id="tv_center" x="120" y="70" w="100" h="70" text-valign="center">vcenter</label>
      <label id="tv_bottom" x="230" y="70" w="100" h="70" text-valign="bottom">bottom</label>
      
      <!-- Combined -->
      <label id="ta_cc" x="10" y="150" w="100" h="70" text-align="center center">CC</label>
      
      <!-- Element alignment -->
      <label id="pa_center" align="center" y="250">centered</label>
    </page>
  </ui>
</app>
)";

#define TEST(name) printf("  %-50s ", name);
#define PASS() printf("✓\n")
#define FAIL(msg) do { printf("✗ %s\n", msg); failures++; } while(0)

int main() {
    printf("=== QI Alignment Tests ===\n\n");
    int failures = 0;
    
    // Render to mock
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(ALIGN_APP);
    printf("Rendered %d widgets\n\n", count);
    
    // Convert to KDL and parse with UIQuery
    std::string kdl = LvglMock::to_kdl();
    printf("KDL output:\n%s\n", kdl.c_str());
    
    auto qi = UIQuery::parse(kdl);
    
    printf("Testing via UIQuery:\n");
    
    // === Text horizontal alignment ===
    
    TEST("qi.text_align_h('ta_left') == '' (left is default)") {
        // left is default, so it's not output
        if (qi.text_align_h("ta_left") == "") PASS();
        else FAIL(qi.text_align_h("ta_left").c_str());
    }
    
    TEST("qi.text_align_h('ta_center') == 'center'") {
        if (qi.text_align_h("ta_center") == "center") PASS();
        else FAIL(qi.text_align_h("ta_center").c_str());
    }
    
    TEST("qi.text_align_h('ta_right') == 'right'") {
        if (qi.text_align_h("ta_right") == "right") PASS();
        else FAIL(qi.text_align_h("ta_right").c_str());
    }
    
    // === Vertical alignment via pad-top ===
    
    TEST("qi.pad_top('tv_top') == 0") {
        if (qi.pad_top("tv_top") == 0) PASS();
        else { FAIL(""); printf("      got %d\n", qi.pad_top("tv_top")); }
    }
    
    TEST("qi.pad_top('tv_center') in range 20-35") {
        int p = qi.pad_top("tv_center");
        // h=70, fontSize~16 → center ≈ 27
        if (p >= 20 && p <= 35) PASS();
        else { FAIL(""); printf("      got %d (expected 20-35)\n", p); }
    }
    
    TEST("qi.pad_top('tv_bottom') > 45") {
        int p = qi.pad_top("tv_bottom");
        // h=70, fontSize~16 → bottom ≈ 54
        if (p > 45) PASS();
        else { FAIL(""); printf("      got %d (expected >45)\n", p); }
    }
    
    // === Combined ===
    
    TEST("qi.text_align_h('ta_cc') == 'center' && pad_top > 0") {
        if (qi.text_align_h("ta_cc") == "center" && qi.pad_top("ta_cc") > 0) PASS();
        else { FAIL(""); printf("      got h=%s pad=%d\n", 
            qi.text_align_h("ta_cc").c_str(), qi.pad_top("ta_cc")); }
    }
    
    // === Element alignment ===
    
    TEST("qi.align_type('pa_center') != 0 (align called)") {
        if (qi.align_type("pa_center") != 0) PASS();
        else FAIL("");
    }
    
    // === Position/size ===
    
    TEST("qi.x('ta_left') == 10") {
        if (qi.x("ta_left") == 10) PASS();
        else { FAIL(""); printf("      got %d\n", qi.x("ta_left")); }
    }
    
    TEST("qi.width('ta_left') == 100") {
        if (qi.width("ta_left") == 100) PASS();
        else { FAIL(""); printf("      got %d\n", qi.width("ta_left")); }
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
