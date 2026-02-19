/**
 * Test: CSS Selectors — tag, .class, tag.class, #id
 * 
 * Cascade order: tag < .class < tag.class < #id < inline
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* CSS_APP = R"(
<app>
  <style>
    /* Class selectors */
    .primary { background: #0066ff; color: #ffffff; }
    .danger { background: #ff4444; color: #ffffff; }
    .success { background: #44ff44; }
    .large { font-size: 32; }
    .rounded { border-radius: 16; }
    .box { background: #222222; border-radius: 8; }
    .title { font-size: 48; color: #ffff00; }
    .faded { opacity: 0.5; }

    /* Tag selectors */
    button { background: #333333; border-radius: 4; }
    label { color: #aaaaaa; }

    /* Tag.class selectors */
    button.accent { background: #ff9500; }

    /* ID selectors */
    #hero { font-size: 72; color: #ff0000; }
    #special { border-radius: 20; }
    #idWins { color: #00ff00; }

    /* Cascade: tag.class > .class */
    .blueTxt { color: #0000ff; }
    button.blueTxt { color: #ff00ff; }

    /* Cascade: tag.class > tag */
    label.bigFont { font-size: 48; }

    /* Later rule wins (same specificity) */
    .earlyColor { color: #111111; }
    .earlyColor { color: #222222; }

    /* CSS layout properties */
    .padded { padding: 10; }
    .sized { width: 200; height: 80; }
    .positioned { left: 50; top: 30; }
  </style>
  
  <ui default="/main">
    <page id="main">
      <!-- Single class -->
      <button id="btn_primary" x="10" y="10" w="100" h="40" class="primary">Primary</button>
      <button id="btn_danger" x="120" y="10" w="100" h="40" class="danger">Danger</button>
      <button id="btn_success" x="230" y="10" w="100" h="40" class="success">Success</button>
      
      <!-- Multiple classes -->
      <button id="btn_multi" x="10" y="60" w="100" h="40" class="primary rounded">Multi</button>
      
      <!-- Class on label -->
      <label id="lbl_title" x="10" y="110" class="title">Title</label>
      <label id="lbl_large" x="10" y="170" class="large">Large Text</label>
      
      <!-- Inline override -->
      <button id="btn_override" x="10" y="220" w="100" h="40" class="primary" bgcolor="#00ff00">Override</button>
      
      <!-- Box -->
      <label id="lbl_box" x="10" y="270" w="200" h="60" class="box">Box</label>
      
      <!-- No class - baseline -->
      <button id="btn_plain" x="10" y="340" w="100" h="40">Plain</button>
      
      <!-- Opacity -->
      <label id="lbl_faded" x="10" y="390" class="faded">Faded</label>

      <!-- TAG selector tests -->
      <button id="btn_noclass" x="10" y="430" w="100" h="40">No Class</button>
      <label id="lbl_noclass" x="120" y="430">No Class Label</label>

      <!-- TAG.CLASS selector -->
      <button id="btn_accent" x="10" y="480" w="100" h="40" class="accent">Accent</button>
      <label id="lbl_accent" x="120" y="480" class="accent">Not Button</label>

      <!-- ID selector -->
      <label id="hero" x="10" y="530">Hero</label>

      <!-- Cascade: tag < class < id -->
      <button id="special" x="10" y="580" w="100" h="40" class="primary">Cascade</button>

      <!-- #id > .class: both set color, id should win -->
      <label id="idWins" x="10" y="620" class="blueTxt">ID wins</label>

      <!-- tag.class > .class -->
      <button id="btn_tagclass" x="10" y="660" w="100" h="40" class="blueTxt">TagClass</button>

      <!-- tag.class > tag (label tag sets color #aaa, label.bigFont sets font 48) -->
      <label id="lbl_bigfont" x="10" y="700" class="bigFont">Big</label>

      <!-- Later rule wins same specificity -->
      <label id="lbl_later" x="10" y="740" class="earlyColor">Later</label>

      <!-- CSS layout properties -->
      <label id="lbl_padded" x="0" y="780" w="100" h="40" class="padded">Padded</label>
      <label id="lbl_sized" x="0" y="820" class="sized">Sized</label>
      <label id="lbl_pos" class="positioned">Pos</label>
    </page>
  </ui>
</app>
)";

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== CSS Selector Tests ===\n\n");
    int passed = 0;
    int total = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(CSS_APP);
    printf("Rendered %d widgets\n\n", count);
    
    auto* page = LvglMock::g_screen->first("Container");
    if (!page) {
        printf("FATAL: No page\n");
        return 1;
    }
    
    // === Single class ===
    printf("Single class:\n");
    
    TEST(".primary -> bgcolor #0066ff");
    {
        auto* w = page->findById("btn_primary");
        if (w && w->hasBgcolor && w->bgcolor == 0x0066ff) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x hasBgcolor=%d\n", w->bgcolor, w->hasBgcolor); }
    }
    
    TEST(".primary -> color #ffffff");
    {
        auto* w = page->findById("btn_primary");
        if (w && w->hasColor && w->color == 0xffffff) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x hasColor=%d\n", w->color, w->hasColor); }
    }
    
    TEST(".danger -> bgcolor #ff4444");
    {
        auto* w = page->findById("btn_danger");
        if (w && w->hasBgcolor && w->bgcolor == 0xff4444) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    TEST(".success -> bgcolor #44ff44");
    {
        auto* w = page->findById("btn_success");
        if (w && w->hasBgcolor && w->bgcolor == 0x44ff44) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    // === Multiple classes ===
    printf("\nMultiple classes:\n");
    
    TEST(".primary .rounded -> bgcolor #0066ff");
    {
        auto* w = page->findById("btn_multi");
        if (w && w->hasBgcolor && w->bgcolor == 0x0066ff) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    TEST(".primary .rounded -> radius 16");
    {
        auto* w = page->findById("btn_multi");
        if (w && w->radius == 16) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->radius); }
    }
    
    // === Label classes ===
    printf("\nLabel classes:\n");
    
    TEST(".title -> font-size 48");
    {
        auto* w = page->findById("lbl_title");
        if (w && w->fontSize == 48) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->fontSize); }
    }
    
    TEST(".title -> color #ffff00");
    {
        auto* w = page->findById("lbl_title");
        if (w && w->hasColor && w->color == 0xffff00) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }
    
    TEST(".large -> font-size 32");
    {
        auto* w = page->findById("lbl_large");
        if (w && w->fontSize == 32) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->fontSize); }
    }
    
    // === Inline override (standard CSS behavior: inline wins) ===
    printf("\nInline override:\n");
    
    TEST("inline bgcolor=#00ff00 should override .primary");
    {
        auto* w = page->findById("btn_override");
        // Standard CSS: inline attributes have highest specificity
        if (w && w->hasBgcolor && w->bgcolor == 0x00ff00) PASS();
        else { FAIL("BUG: class overrides inline"); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    // === Box class ===
    printf("\nBox class:\n");
    
    TEST(".box -> bgcolor #222222");
    {
        auto* w = page->findById("lbl_box");
        if (w && w->hasBgcolor && w->bgcolor == 0x222222) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }
    
    TEST(".box -> radius 8");
    {
        auto* w = page->findById("lbl_box");
        if (w && w->radius == 8) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->radius); }
    }
    
    // === No class baseline ===
    printf("\nNo class (baseline):\n");
    
    TEST("btn_plain gets tag bgcolor #333333");
    {
        auto* w = page->findById("btn_plain");
        if (w && w->hasBgcolor && w->bgcolor == 0x333333) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }
    
    // === Opacity ===
    printf("\nOpacity:\n");
    
    TEST(".faded -> opacity ~128 (0.5 * 255)");
    {
        auto* w = page->findById("lbl_faded");
        // opacity: 0.5 → 127-128
        if (w && w->opacity >= 120 && w->opacity <= 135) PASS();
        else { FAIL(""); if(w) printf("      got %d (expected ~128)\n", w->opacity); }
    }
    
    // === Tag selectors ===
    printf("\nTag selectors:\n");

    TEST("button tag -> bgcolor #333333") {
        auto* w = page->findById("btn_noclass");
        if (w && w->hasBgcolor && w->bgcolor == 0x333333) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }

    TEST("button tag -> radius 4") {
        auto* w = page->findById("btn_noclass");
        if (w && w->radius == 4) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->radius); }
    }

    TEST("label tag -> color #aaaaaa") {
        auto* w = page->findById("lbl_noclass");
        if (w && w->hasColor && w->color == 0xaaaaaa) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x hasColor=%d\n", w->color, w->hasColor); }
    }

    // === Tag.class selectors ===
    printf("\nTag.class selectors:\n");

    TEST("button.accent -> bgcolor #ff9500 (button + accent)") {
        auto* w = page->findById("btn_accent");
        if (w && w->hasBgcolor && w->bgcolor == 0xff9500) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }

    TEST("label.accent -> no bgcolor change (button.accent != label)") {
        auto* w = page->findById("lbl_accent");
        // label tag sets color #aaa but no bgcolor; button.accent shouldn't match label
        if (w && !w->hasBgcolor) PASS();
        else { FAIL("button.accent incorrectly matched label"); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }

    // === ID selectors ===
    printf("\nID selectors:\n");

    TEST("#hero -> font-size 72") {
        auto* w = page->findById("hero");
        if (w && w->fontSize == 72) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->fontSize); }
    }

    TEST("#hero -> color #ff0000") {
        auto* w = page->findById("hero");
        if (w && w->hasColor && w->color == 0xff0000) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }

    // === Cascade: tag < class < id ===
    printf("\nCascade (tag < class < #id):\n");

    TEST("#special has .primary bgcolor #0066ff (class > tag)") {
        auto* w = page->findById("special");
        // tag: #333, class: #0066ff, id: no bgcolor → class wins
        if (w && w->hasBgcolor && w->bgcolor == 0x0066ff) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
    }

    TEST("#special has radius 20 (id > tag's 4)") {
        auto* w = page->findById("special");
        if (w && w->radius == 20) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->radius); }
    }

    // === #id > .class ===
    printf("\nCascade (#id > .class):\n");

    TEST("#idWins color #00ff00 beats .blueTxt #0000ff") {
        auto* w = page->findById("idWins");
        if (w && w->hasColor && w->color == 0x00ff00) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }

    // === tag.class > .class ===
    printf("\nCascade (tag.class > .class):\n");

    TEST("button.blueTxt #ff00ff beats .blueTxt #0000ff") {
        auto* w = page->findById("btn_tagclass");
        if (w && w->hasColor && w->color == 0xff00ff) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }

    // === tag.class > tag ===
    printf("\nCascade (tag.class > tag):\n");

    TEST("label.bigFont font 48 (tag.class applied)") {
        auto* w = page->findById("lbl_bigfont");
        if (w && w->fontSize == 48) PASS();
        else { FAIL(""); if(w) printf("      got %d\n", w->fontSize); }
    }

    TEST("label.bigFont still has tag color #aaaaaa") {
        auto* w = page->findById("lbl_bigfont");
        if (w && w->hasColor && w->color == 0xaaaaaa) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }

    // === Later rule wins (same specificity) ===
    printf("\nLater rule wins:\n");

    TEST(".earlyColor redefined -> color #222222") {
        auto* w = page->findById("lbl_later");
        if (w && w->hasColor && w->color == 0x222222) PASS();
        else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
    }

    // === CSS layout properties ===
    printf("\nCSS layout properties:\n");

    TEST("padding: 10 applied") {
        auto* w = page->findById("lbl_padded");
        if (w && w->padAll == 10) PASS();
        else { FAIL(""); if(w) printf("      got padAll=%d\n", w->padAll); }
    }

    TEST("width: 200, height: 80 from CSS") {
        auto* w = page->findById("lbl_sized");
        if (w && w->w == 200 && w->h == 80) PASS();
        else { FAIL(""); if(w) printf("      got %dx%d\n", w->w, w->h); }
    }

    TEST("left: 50, top: 30 from CSS") {
        auto* w = page->findById("lbl_pos");
        if (w && w->x == 50 && w->y == 30) PASS();
        else { FAIL(""); if(w) printf("      got x=%d y=%d\n", w->x, w->y); }
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d CSS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
