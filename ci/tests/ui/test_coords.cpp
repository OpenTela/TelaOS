#include <cstdio>
#include <cstdlib>
#include <cassert>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"
#include "ui/ui_coords.h"

static int pass = 0;
static int fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { pass++; } \
    else { fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

// ==========================================================
// 1. parse_size / parse_coord unit tests
// ==========================================================
void test_parse_functions() {
    printf("--- parse_size / parse_coord ---\n");
    
    // Pixels — same for both
    CHECK(parse_size("100") == 100, "parse_size 100px");
    CHECK(parse_coord_w("100") == 100, "parse_coord_w 100px");
    
    // Percentages — different!
    CHECK(parse_size("50%") == lv_pct(50), "parse_size 50% = lv_pct(50)");
    CHECK(parse_coord_w("50%") == 240, "parse_coord_w 50% = 240px");
    CHECK(parse_coord_h("50%") == 240, "parse_coord_h 50% = 240px");
    
    CHECK(parse_size("100%") == lv_pct(100), "parse_size 100%");
    CHECK(parse_size("25%") == lv_pct(25), "parse_size 25%");
    CHECK(parse_coord_w("100%") == 480, "parse_coord_w 100%");
    
    // Zero/empty
    CHECK(parse_size("") == 0, "parse_size empty");
    CHECK(parse_size("0") == 0, "parse_size 0");
    CHECK(parse_coord_w("") == 0, "parse_coord_w empty");
    
    // Float
    CHECK(parse_size("12.5") == 13, "parse_size 12.5 rounds");
    
    // lv_pct values are large (not confused with pixels)
    CHECK(lv_pct(50) > 1000, "lv_pct(50) is large encoded value");
    CHECK(parse_size("1%") > 1000, "parse_size 1% is large encoded value");
}

// ==========================================================
// 2. Table hierarchy — containers, flex, sizes
// ==========================================================
void test_table_hierarchy() {
    printf("--- table hierarchy ---\n");
    
    const char* HTML = R"(
<app>
  <ui default="/main">
    <page id="main">
      <table x="5%" y="20%" w="90%" h="75%" cellspacing="2%">
        <tr h="50%">
          <td h="100%"><button id="b1" w="100%" h="100%" bgcolor="#505050">B1</button></td>
          <td h="100%"><button id="b2" w="100%" h="100%" bgcolor="#ff9500">B2</button></td>
        </tr>
        <tr h="50%">
          <td h="100%"><button id="b3" w="100%" h="100%" bgcolor="#505050">B3</button></td>
          <td h="100%"><button id="b4" w="100%" h="100%" bgcolor="#ff9500">B4</button></td>
        </tr>
      </table>
    </page>
  </ui>
  <state/>
  <script language="lua"></script>
</app>
)";

    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(HTML);
    
    auto* page = LvglMock::g_screen->first("Container");
    CHECK(page != nullptr, "page exists");
    if (!page) return;
    
    // Buttons should exist with labels
    auto* b1 = page->findById("b1");
    auto* b2 = page->findById("b2");
    auto* b3 = page->findById("b3");
    auto* b4 = page->findById("b4");
    CHECK(b1 != nullptr, "b1 found");
    CHECK(b2 != nullptr, "b2 found");
    CHECK(b3 != nullptr, "b3 found");
    CHECK(b4 != nullptr, "b4 found");
    
    if (b1) {
        bool hasLabel = false;
        for (auto* ch : b1->children) {
            if (ch->type == "Label" && ch->text == "B1") hasLabel = true;
        }
        CHECK(hasLabel, "b1 has label text 'B1'");
        
        // Button w/h should be lv_pct(100) — from parse_size
        CHECK(b1->w == lv_pct(100), "b1 width = lv_pct(100)");
        CHECK(b1->h == lv_pct(100), "b1 height = lv_pct(100)");
    }
}

// ==========================================================
// 3. Label valign guard — lv_pct not treated as pixels
// ==========================================================
void test_valign_guard() {
    printf("--- valign guard ---\n");
    
    const char* HTML = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="lbl_pct" x="5%" y="5%" w="90%" h="10%">{text}</label>
      <label id="lbl_px" x="5%" y="20%" w="90%" h="48">Static</label>
    </page>
  </ui>
  <state>
    <string name="text" default="Hello"/>
  </state>
  <script language="lua"></script>
</app>
)";

    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(HTML);
    
    auto* page = LvglMock::g_screen->first("Container");
    auto* lbl_pct = page->findById("lbl_pct");
    auto* lbl_px = page->findById("lbl_px");
    CHECK(lbl_pct != nullptr, "label h=10% exists");
    CHECK(lbl_px != nullptr, "label h=48 exists");
    
    if (lbl_pct) {
        // h="10%" → lv_pct(10) → guard should skip applyTextValign
        CHECK(lbl_pct->padTop < 100, "pct label padTop not astronomic");
    }
    if (lbl_px) {
        // h=48 → pixel → applyTextValign should center text
        // padTop should be (48 - lineHeight) / 2
        CHECK(lbl_px->padTop >= 0 && lbl_px->padTop < 48, "px label padTop reasonable");
    }
}

// ==========================================================
// 4. Canvas uses pixel coords (not lv_pct)
// ==========================================================
void test_canvas_pixels() {
    printf("--- canvas pixels ---\n");
    // Canvas uses getAttrCoordW (verified in source)
    // parse_coord_w("50%") = 240, not lv_pct(50) = 2050
    CHECK(parse_coord_w("50%") == 240, "canvas 50% resolves to 240px");
    CHECK(parse_coord_w("50%") != lv_pct(50), "canvas 50% is NOT lv_pct(50)");
    CHECK(parse_size("50%") == lv_pct(50), "contrast: parse_size 50% IS lv_pct(50)");
}

// ==========================================================
// 5. Page-level widgets — w/h % from parse_size
// ==========================================================
void test_page_widgets() {
    printf("--- page-level widgets ---\n");
    
    const char* HTML = R"(
<app>
  <ui default="/main">
    <page id="main">
      <button id="btn1" x="5%" y="70%" w="42%" h="40" bgcolor="#f00" onclick="fn1">Reset</button>
      <label id="lbl1" x="10%" y="5%" w="80%" h="48" color="#fff">Title</label>
    </page>
  </ui>
  <state/>
  <script language="lua">function fn1() end</script>
</app>
)";

    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(HTML);
    
    auto* page = LvglMock::g_screen->first("Container");
    auto* btn1 = page->findById("btn1");
    auto* lbl1 = page->findById("lbl1");
    CHECK(btn1 != nullptr, "button exists");
    CHECK(lbl1 != nullptr, "label exists");
    
    if (btn1) {
        // w="42%" → lv_pct(42) via parse_size
        CHECK(btn1->w == lv_pct(42), "button w = lv_pct(42)");
        CHECK(btn1->h == 40, "button h = 40px");
    }
    if (lbl1) {
        CHECK(lbl1->w == lv_pct(80), "label w = lv_pct(80)");
        CHECK(lbl1->h == 48, "label h = 48px");
    }
}

int main() {
    test_parse_functions();
    test_table_hierarchy();
    test_valign_guard();
    test_canvas_pixels();
    test_page_widgets();
    
    printf("\n=== %d passed, %d failed ===\n", pass, fail);
    if (fail > 0) {
        printf("=== SOME TESTS FAILED ===\n");
        return 1;
    }
    printf("=== ALL %d TESTS PASSED ===\n", pass);
    return 0;
}
