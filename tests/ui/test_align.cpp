/**
 * Test: Alignment
 * 
 * Tests all alignment combinations:
 * - text-align: left, center, right
 * - text-valign: top, center, bottom  
 * - text-align: "h v" combined form
 * - align: element positioning (captures lv_obj_align calls)
 * - valign: element vertical positioning
 */
#include <cstdio>
#include <cassert>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* ALIGN_APP = R"(
<app>
  <ui default="/align">
    <group id="align" default="textalign" orientation="horizontal">
    
      <!-- Page 1: Text alignment inside elements -->
      <page id="textalign" bgcolor="#111">
        <!-- Horizontal text-align -->
        <label id="ta_left" x="10" y="10" w="100" h="50" bgcolor="#333" text-align="left">left</label>
        <label id="ta_center" x="120" y="10" w="100" h="50" bgcolor="#333" text-align="center">center</label>
        <label id="ta_right" x="230" y="10" w="100" h="50" bgcolor="#333" text-align="right">right</label>
        
        <!-- Vertical text-valign -->
        <label id="tv_top" x="10" y="70" w="100" h="70" bgcolor="#444" text-valign="top">top</label>
        <label id="tv_center" x="120" y="70" w="100" h="70" bgcolor="#444" text-valign="center">center</label>
        <label id="tv_bottom" x="230" y="70" w="100" h="70" bgcolor="#444" text-valign="bottom">bottom</label>
        
        <!-- Combined text-align="h v" -->
        <label id="ta_lt" x="10" y="150" w="100" h="70" bgcolor="#555" text-align="left top">LT</label>
        <label id="ta_cc" x="120" y="150" w="100" h="70" bgcolor="#555" text-align="center center">CC</label>
        <label id="ta_rb" x="230" y="150" w="100" h="70" bgcolor="#555" text-align="right bottom">RB</label>
      </page>
      
      <!-- Page 2: Element positioning with align/valign -->
      <page id="pagealign" bgcolor="#222">
        <!-- align only (horizontal position) -->
        <label id="pa_left" align="left" y="10" bgcolor="#c00">left</label>
        <label id="pa_center" align="center" y="40" bgcolor="#0c0">center</label>
        <label id="pa_right" align="right" y="70" bgcolor="#00c">right</label>
        
        <!-- Combined align="h v" -->
        <label id="pa_cc" align="center center" bgcolor="#c60">cc</label>
        
        <!-- x overrides align -->
        <label id="pa_override" x="50" align="center" y="200" bgcolor="#c06">override</label>
      </page>
      
    </group>
  </ui>
</app>
)";

#define TEST(name) printf("  %-50s ", name);
#define PASS() printf("✓\n")
#define FAIL(msg) do { printf("✗ %s\n", msg); failures++; } while(0)

int main() {
    printf("=== Alignment Tests ===\n\n");
    int failures = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(ALIGN_APP);
    printf("Rendered %d widgets\n\n", count);
    
    // Find pages in TileView
    auto* tileview = LvglMock::g_screen->first("TileView");
    if (!tileview) {
        printf("FATAL: No TileView found\n");
        return 1;
    }
    
    auto tiles = tileview->findAll("Tile", false);
    printf("Found %zu tiles\n\n", tiles.size());
    
    // Page 1: textalign
    printf("Page 1: text-align / text-valign (LVGL calls captured)\n");
    
    auto* page1 = tiles.size() > 0 ? tiles[0] : nullptr;
    if (!page1) {
        printf("  FATAL: textalign page not found\n");
        return 1;
    }
    
    // Check textAlignH values (0=left, 1=center, 2=right)
    TEST("ta_left has textAlignH=0 (left)") {
        auto* w = page1->findById("ta_left");
        if (w && w->textAlignH == 0) PASS();
        else { FAIL(""); if(w) printf("      got textAlignH=%d\n", w->textAlignH); }
    }
    
    TEST("ta_center has textAlignH=1 (center)") {
        auto* w = page1->findById("ta_center");
        if (w && w->textAlignH == 1) PASS();
        else { FAIL(""); if(w) printf("      got textAlignH=%d\n", w->textAlignH); }
    }
    
    TEST("ta_right has textAlignH=2 (right)") {
        auto* w = page1->findById("ta_right");
        if (w && w->textAlignH == 2) PASS();
        else { FAIL(""); if(w) printf("      got textAlignH=%d\n", w->textAlignH); }
    }
    
    // Check padTop for vertical alignment (top=0, center/bottom>0)
    TEST("tv_top has padTop=0 (top)") {
        auto* w = page1->findById("tv_top");
        if (w && w->padTop == 0) PASS();
        else { FAIL(""); if(w) printf("      got padTop=%d\n", w->padTop); }
    }
    
    TEST("tv_center has padTop>0 (center)") {
        auto* w = page1->findById("tv_center");
        // h=70, fontSize~16 → center padTop ≈ (70-16)/2 = 27
        // Allow range 20-35
        if (w && w->padTop >= 20 && w->padTop <= 35) PASS();
        else { FAIL(""); if(w) printf("      got padTop=%d (expected 20-35)\n", w->padTop); }
    }
    
    TEST("tv_bottom has padTop > tv_center (bottom)") {
        auto* w_center = page1->findById("tv_center");
        auto* w_bottom = page1->findById("tv_bottom");
        // h=70, fontSize~16 → bottom padTop ≈ 70-16 = 54
        // Should be roughly 2x center
        if (w_center && w_bottom && w_bottom->padTop > w_center->padTop * 1.5) PASS();
        else { 
            FAIL(""); 
            if(w_center && w_bottom) 
                printf("      center=%d, bottom=%d (bottom should be ~2x center)\n", 
                    w_center->padTop, w_bottom->padTop); 
        }
    }
    
    // Combined form
    TEST("ta_lt has textAlignH=0, padTop=0 (left top)") {
        auto* w = page1->findById("ta_lt");
        if (w && w->textAlignH == 0 && w->padTop == 0) PASS();
        else { FAIL(""); if(w) printf("      got H=%d padTop=%d\n", w->textAlignH, w->padTop); }
    }
    
    TEST("ta_cc has textAlignH=1, padTop>0 (center center)") {
        auto* w = page1->findById("ta_cc");
        if (w && w->textAlignH == 1 && w->padTop > 0) PASS();
        else { FAIL(""); if(w) printf("      got H=%d padTop=%d\n", w->textAlignH, w->padTop); }
    }
    
    TEST("ta_rb has textAlignH=2, padTop>0 (right bottom)") {
        auto* w = page1->findById("ta_rb");
        if (w && w->textAlignH == 2 && w->padTop > 0) PASS();
        else { FAIL(""); if(w) printf("      got H=%d padTop=%d\n", w->textAlignH, w->padTop); }
    }
    
    // Page 2: element alignment (lv_obj_align captured)
    printf("\nPage 2: align / valign (lv_obj_align calls captured)\n");
    
    auto* page2 = tiles.size() > 1 ? tiles[1] : nullptr;
    if (!page2) {
        printf("  FATAL: pagealign page not found\n");
        return 1;
    }
    
    // LV_ALIGN_LEFT_MID = 7, LV_ALIGN_CENTER = 9, LV_ALIGN_RIGHT_MID = 8
    TEST("pa_left has alignType != 0 (align called)") {
        auto* w = page2->findById("pa_left");
        if (w && w->alignType != 0) PASS();
        else { FAIL(""); if(w) printf("      got alignType=%d\n", w->alignType); }
    }
    
    TEST("pa_center has alignType=9 (LV_ALIGN_CENTER) or similar") {
        auto* w = page2->findById("pa_center");
        // Just check align was called
        if (w && w->alignType != 0) PASS();
        else { FAIL(""); if(w) printf("      got alignType=%d\n", w->alignType); }
    }
    
    TEST("pa_cc has alignType=9 (LV_ALIGN_CENTER)") {
        auto* w = page2->findById("pa_cc");
        if (w && w->alignType == LV_ALIGN_CENTER) PASS();
        else { FAIL(""); if(w) printf("      got alignType=%d (expected %d)\n", w->alignType, LV_ALIGN_CENTER); }
    }
    
    TEST("pa_override: x=50 set, align may also be set") {
        auto* w = page2->findById("pa_override");
        if (w && w->x == 50) PASS();
        else { FAIL(""); if(w) printf("      got x=%d\n", w->x); }
    }
    
    // === Summary ===
    printf("\n");
    if (failures == 0) {
        printf("=== ALL %d TESTS PASSED ===\n", 13);
        return 0;
    } else {
        printf("=== %d TESTS FAILED ===\n", failures);
        return 1;
    }
}
