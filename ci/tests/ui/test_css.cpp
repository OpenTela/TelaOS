/**
 * Test: CSS Selectors — tag, .class, tag.class, #id
 * 
 * Cascade order: tag < .class < tag.class < #id < inline
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
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

    /* --- Extended test rules --- */

    /* Multi-class conflict: both set bgcolor, later wins */
    .bgRed { background: #ff0000; }
    .bgBlue { background: #0000ff; }

    /* #RGB shorthand */
    .shortColor { color: #f00; background: #0f0; }

    /* Zero value */
    .noRadius { border-radius: 0; }

    /* Full composite: one rule, many props */
    .composite { background: #abcdef; color: #fedcba; border-radius: 12; font-size: 32; }

    /* Font on button */
    .btnFont { font-size: 24; }

    /* Tag selector for input */
    input { background: #444444; }

    /* Cascade layering: tag + class + id all different props */
    .layerColor { color: #00ffff; }
    #layered { font-size: 64; }

    /* Double class same property */
    .colorA { color: #aabbcc; }
    .colorB { color: #ddeeff; }

    /* Orphan rule — matches nothing */
    .ghostRule { background: #999999; font-size: 99; }
    #noSuchId { color: #123456; }
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

      <!-- Extended tests -->
      <!-- Unknown/empty class -->
      <button id="btn_ghost" x="10" y="860" w="80" h="30" class="nonexistent">Ghost</button>
      <button id="btn_empty_class" x="100" y="860" w="80" h="30" class="">Empty</button>

      <!-- Class on input/slider/switch -->
      <input id="inp_styled" x="10" y="900" w="200" h="35" class="primary" bind="dummy1"/>
      <slider id="sld_styled" x="10" y="945" w="200" class="primary" bind="dummy2"/>
      <switch id="sw_styled" x="10" y="985" class="danger" bind="dummy3"/>

      <!-- Multi-class conflict: bgRed then bgBlue, later wins -->
      <button id="btn_conflict" x="10" y="1020" w="100" h="30" class="bgRed bgBlue">Conflict</button>

      <!-- #RGB shorthand -->
      <label id="lbl_short" x="10" y="1060" class="shortColor">Short</label>

      <!-- Zero radius: tag button sets radius 4, .noRadius sets 0 -->
      <button id="btn_norad" x="10" y="1100" w="80" h="30" class="noRadius">NoRad</button>

      <!-- Composite rule: many props at once -->
      <label id="lbl_composite" x="10" y="1140" class="composite">Comp</label>

      <!-- Font on button -->
      <button id="btn_fontsize" x="10" y="1180" w="100" h="30" class="btnFont">Sized</button>

      <!-- Tag selector on input (no class) -->
      <input id="inp_tagonly" x="10" y="1220" w="200" h="35" bind="dummy4"/>

      <!-- Cascade layering: tag(button)=bgcolor#333+radius4, class=color#00ffff, id=font64 -->
      <button id="layered" x="10" y="1260" w="100" h="30" class="layerColor">Layer</button>

      <!-- Inline bgcolor + class color: independent props both apply -->
      <label id="lbl_mixed" x="10" y="1300" bgcolor="#550055" class="blueTxt">Mixed</label>

      <!-- Double class same property: colorA then colorB, B wins -->
      <label id="lbl_double" x="10" y="1340" class="colorA colorB">Double</label>
    </page>
  </ui>
  
  <state>
    <string name="dummy1" default=""/>
    <int name="dummy2" default="50"/>
    <bool name="dummy3" default="false"/>
    <string name="dummy4" default=""/>
  </state>
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
    g_core.store().clear();
    
    int count = g_core.render(CSS_APP);
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

    // === Extended: edge cases & coverage ===
    printf("\nEdge cases:\n");

    TEST("unknown class → no crash, tag style still applied") {
        auto* w = page->findById("btn_ghost");
        // button tag sets bgcolor #333, nonexistent class does nothing
        if (w && w->hasBgcolor && w->bgcolor == 0x333333) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }

    TEST("empty class=\"\" → no crash, tag style applied") {
        auto* w = page->findById("btn_empty_class");
        if (w && w->hasBgcolor && w->bgcolor == 0x333333) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }

    printf("\nClass on different widget types:\n");

    TEST(".primary on input → bgcolor #0066ff") {
        auto* w = page->findById("inp_styled");
        if (w && w->hasBgcolor && w->bgcolor == 0x0066ff) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }

    TEST(".primary on slider → bgcolor #0066ff") {
        auto* w = page->findById("sld_styled");
        if (w && w->hasBgcolor && w->bgcolor == 0x0066ff) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }

    TEST(".danger on switch → bgcolor #ff4444") {
        auto* w = page->findById("sw_styled");
        if (w && w->hasBgcolor && w->bgcolor == 0xff4444) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }

    printf("\nMulti-class conflict:\n");

    TEST("class=\"bgRed bgBlue\" → bgBlue wins (later rule)") {
        auto* w = page->findById("btn_conflict");
        if (w && w->hasBgcolor && w->bgcolor == 0x0000ff) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x\n", w->bgcolor); }
    }

    printf("\n#RGB shorthand:\n");

    TEST("#f00 → color 0xff0000") {
        auto* w = page->findById("lbl_short");
        if (w && w->hasColor && w->color == 0xff0000) PASS();
        else { FAIL(""); if(w) printf("      color=0x%06x\n", w->color); }
    }

    TEST("#0f0 → bgcolor 0x00ff00") {
        auto* w = page->findById("lbl_short");
        if (w && w->hasBgcolor && w->bgcolor == 0x00ff00) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x\n", w->bgcolor); }
    }

    printf("\nZero values:\n");

    TEST(".noRadius → radius 0 overrides tag button radius 4") {
        auto* w = page->findById("btn_norad");
        if (w && w->radius == 0) PASS();
        else { FAIL(""); if(w) printf("      radius=%d\n", w->radius); }
    }

    printf("\nComposite rule (many props):\n");

    TEST(".composite → bgcolor #abcdef") {
        auto* w = page->findById("lbl_composite");
        if (w && w->hasBgcolor && w->bgcolor == 0xabcdef) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x\n", w->bgcolor); }
    }

    TEST(".composite → color #fedcba") {
        auto* w = page->findById("lbl_composite");
        if (w && w->hasColor && w->color == 0xfedcba) PASS();
        else { FAIL(""); if(w) printf("      color=0x%06x\n", w->color); }
    }

    TEST(".composite → radius 12") {
        auto* w = page->findById("lbl_composite");
        if (w && w->radius == 12) PASS();
        else { FAIL(""); if(w) printf("      radius=%d\n", w->radius); }
    }

    TEST(".composite → font-size 32") {
        auto* w = page->findById("lbl_composite");
        if (w && w->fontSize == 32) PASS();
        else { FAIL(""); if(w) printf("      fontSize=%d\n", w->fontSize); }
    }

    printf("\nFont on button:\n");

    TEST(".btnFont → button gets font-size 24→32 (snapped)") {
        auto* w = page->findById("btn_fontsize");
        if (w && w->fontSize == 32) PASS();  // Font::nearest(24) = 32
        else { FAIL(""); if(w) printf("      fontSize=%d\n", w->fontSize); }
    }

    printf("\nTag selector on input:\n");

    TEST("input tag → bgcolor #444444 (no class)") {
        auto* w = page->findById("inp_tagonly");
        if (w && w->hasBgcolor && w->bgcolor == 0x444444) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x hasBg=%d\n", w->bgcolor, w->hasBgcolor); }
    }

    printf("\nCascade layering (tag+class+id):\n");

    TEST("tag→bgcolor #333, class→color #00ffff, id→font 64→72 (snapped)") {
        auto* w = page->findById("layered");
        bool bgOk = w && w->hasBgcolor && w->bgcolor == 0x333333;
        bool colorOk = w && w->hasColor && w->color == 0x00ffff;
        bool fontOk = w && w->fontSize == 72;  // Font::nearest(64) = 72
        if (bgOk && colorOk && fontOk) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x color=0x%06x font=%d\n", w->bgcolor, w->color, w->fontSize); }
    }

    printf("\nInline + class independent props:\n");

    TEST("inline bgcolor=#550055 + .blueTxt color=#0000ff — both work") {
        auto* w = page->findById("lbl_mixed");
        bool bgOk = w && w->hasBgcolor && w->bgcolor == 0x550055;
        bool colorOk = w && w->hasColor && w->color == 0x0000ff;
        if (bgOk && colorOk) PASS();
        else { FAIL(""); if(w) printf("      bg=0x%06x color=0x%06x\n", w->bgcolor, w->color); }
    }

    printf("\nDouble class same prop:\n");

    TEST("class=\"colorA colorB\" → colorB #ddeeff wins") {
        auto* w = page->findById("lbl_double");
        if (w && w->hasColor && w->color == 0xddeeff) PASS();
        else { FAIL(""); if(w) printf("      color=0x%06x\n", w->color); }
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
