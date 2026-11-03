/**
 * Test: CSS structure and screen — @media, an element's place among its siblings
 * (:first-child, :last-child, :only-child, :nth-child() and the -last- and -of-type forms)
 * and the sibling combinators (h2 + p, h2 ~ p). See docs/CSS.md.
 */
#include <cstdio>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
#include "ui/css_media.h"

using MW = LvglMock::Widget;

static const char* APP = R"HTML(
<app>
  <style>
    @media (max-width: 500px) { .m1 { color: #000001; } }
    @media (min-width: 600px) { .m2 { color: #000002; } }
    @media screen and (orientation: portrait) { .m3 { color: #000003; } }
    @media print { .m4 { color: #000004; } }
    @media not print { .m5 { color: #000005; } }
    @media (400px <= width <= 500px) { .m6 { color: #000006; } }
    @media (hover: none) and (pointer: coarse) { .m7 { color: #000007; } }
    @media (prefers-color-scheme: dark) { .m8 { color: #000008; } }
    @media (min-width: 30em) { .m9 { color: #000009; } }
    @media (width > 480px) { .m10 { color: #00000A; } }
    @media (unknown-thing: 1) { .m11 { color: #00000B; } }
    @media not (unknown-thing: 1) { .m12 { color: #00000C; } }
    @media (max-width: 100px), print, (min-height: 400px) { .m13 { color: #00000D; } }
    @media screen { @media (min-width: 400px) { .m14 { color: #00000E; } } }
    .m15 { color: #00000F; }
    @media all { .m15 { color: #000010; } }
    @media (max-width: 479.98px) { .m16 { color: #000011; } }
    @media (aspect-ratio: 1/1) { .m17 { color: #000012; } }
    @media (min-resolution: 2dppx) { .m18 { color: #000013; } }
    @media (max-width: 500px) and print { .m19 { color: #000014; } }

    #la li:nth-child(odd) { color: #0A0001; }
    #la li:nth-child(even) { color: #0A0002; }
    #lb li:nth-child(3n+1) { color: #0B0001; }
    #lb li:nth-last-child(2) { color: #0B0002; }
    #lb li:nth-child(2n+9) { color: #0B0003; }
    #lc li:first-child { color: #0C0001; }
    #lc li:last-child { color: #0C0002; }
    #ld li:only-child { color: #0D0001; }
    #mix p:first-of-type { color: #0E0001; }
    #mix p:last-of-type { color: #0E0002; }
    #mix h3:nth-of-type(2) { color: #0E0003; }
    #mix :nth-child(3) { color: #0E0004; }
    #le li:first-child label { color: #0F0001; }
    .sp:first-child { color: #070001; }
    li.sp { color: #070002; }
    li:nth-child(foo), .ok2 { color: #070003; }

    .a + .b { color: #050001; }
    .a ~ .b { border: 1px solid #050002; }
    .c + .b { text-decoration: underline; }
    .b + .a { color: #050003; }
    #sib label:last-child { letter-spacing: 2px; }
    #t1 + p { color: #060001; }
    #t1 ~ p { letter-spacing: 3px; }
    #lz li:last-child { color: #090001; }
    #two .mail + p { color: #090002; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="m1" id="m1">m1</label>
      <label class="m2" id="m2">m2</label>
      <label class="m3" id="m3">m3</label>
      <label class="m4" id="m4">m4</label>
      <label class="m5" id="m5">m5</label>
      <label class="m6" id="m6">m6</label>
      <label class="m7" id="m7">m7</label>
      <label class="m8" id="m8">m8</label>
      <label class="m9" id="m9">m9</label>
      <label class="m10" id="m10">m10</label>
      <label class="m11" id="m11">m11</label>
      <label class="m12" id="m12">m12</label>
      <label class="m13" id="m13">m13</label>
      <label class="m14" id="m14">m14</label>
      <label class="m15" id="m15">m15</label>
      <label class="m16" id="m16">m16</label>
      <label class="m17" id="m17">m17</label>
      <label class="m18" id="m18">m18</label>
      <label class="m19" id="m19">m19</label>

      <ul id="la"><li id="a1">1</li><li id="a2">2</li><li id="a3">3</li><li id="a4">4</li><li id="a5">5</li></ul>
      <ul id="lb"><li id="b1">1</li><li id="b2">2</li><li id="b3">3</li><li id="b4">4</li>
        <li id="b5">5</li><li id="b6">6</li><li id="b7">7</li></ul>
      <ul id="lc"><li id="c1">1</li><li id="c2">2</li><li id="c3">3</li></ul>
      <ul id="ld"><li id="d1">only</li></ul>
      <section id="mix"><h3 id="h1">h</h3><p id="p1">p1</p><p id="p2">p2</p><h3 id="h2">h2</h3><p id="p3">p3</p></section>
      <ul id="le"><li id="e1"><label id="el1">x</label></li><li id="e2"><label id="el2">y</label></li></ul>
      <ul id="lf"><li class="sp" id="f1">1</li><li class="sp" id="f2">2</li></ul>
      <label class="ok2" id="ok2">ok2</label>

      <div id="sib">
        <label class="a" id="s1">a</label>
        <label class="b" id="s2">b</label>
        <label class="c" id="s3">c</label>
        <label class="b" id="s4">b2</label>
      </div>
      <p id="t1">x</p> some text <!-- a comment --> <p id="t2">y</p> <p id="t3">z</p>
    </page>
    <page id="two">
      <p class="mail" id="mail">me@watch: an @ makes the walker build the page from a copy</p>
      <p id="z0">after the mail</p>
      <ul id="lz"><li id="z1">1</li><li id="z2">2</li></ul>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

static std::string hex(long long c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%06llX", c);
    return buf;
}

static std::string colorOf(const MW* w) {
    return !w ? "no widget" : w->hasColor ? "color " + hex(w->color) : "color not set";
}

static void expectTrue(const char* name, bool ok, const std::string& why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why.c_str());
    }
}

static void expectColor(const char* name, const char* id, uint32_t want) {
    const MW* w = byId(id);
    expectTrue(name, w && w->hasColor && w->color == want, colorOf(w));
}

static void expectNoColor(const char* name, const char* id) {
    const MW* w = byId(id);
    expectTrue(name, w && !w->hasColor, colorOf(w));
}

int main() {
    printf("=== CSS STRUCTURE AND SCREEN ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("@media (the screen is 480x480, touch, dark):\n");
    expectColor("(max-width: 500px)", "m1", 0x000001);
    expectNoColor("(min-width: 600px): left out", "m2");
    expectColor("screen and (orientation: portrait)", "m3", 0x000003);
    expectNoColor("print: left out", "m4");
    expectColor("not print", "m5", 0x000005);
    expectColor("(400px <= width <= 500px)", "m6", 0x000006);
    expectColor("(hover: none) and (pointer: coarse)", "m7", 0x000007);
    expectColor("(prefers-color-scheme: dark)", "m8", 0x000008);
    expectColor("(min-width: 30em): 480px", "m9", 0x000009);
    expectNoColor("(width > 480px): left out", "m10");
    expectNoColor("an unknown feature: left out", "m11");
    expectNoColor("not (an unknown feature): left out too", "m12");
    expectColor("a list: any query that holds", "m13", 0x00000D);
    expectColor("@media inside @media", "m14", 0x00000E);
    expectColor("its rules keep their place: after .m15 {}", "m15", 0x000010);
    expectNoColor("(max-width: 479.98px): left out", "m16");
    expectColor("(aspect-ratio: 1/1)", "m17", 0x000012);
    expectNoColor("(min-resolution: 2dppx): left out, 130dpi", "m18");
    expectNoColor("(max-width: 500px) and print: does not read", "m19");

    printf("\nPlace among siblings:\n");
    expectColor(":nth-child(odd): the 1st", "a1", 0x0A0001);
    expectColor(":nth-child(even): the 2nd", "a2", 0x0A0002);
    expectColor(":nth-child(odd): the 5th", "a5", 0x0A0001);
    expectColor(":nth-child(3n+1): the 4th", "b4", 0x0B0001);
    expectColor(":nth-child(3n+1): the 7th", "b7", 0x0B0001);
    expectColor(":nth-last-child(2): the 6th of 7", "b6", 0x0B0002);
    expectNoColor(":nth-child(3n+1): not the 2nd", "b2");
    expectColor(":first-child", "c1", 0x0C0001);
    expectColor(":last-child", "c3", 0x0C0002);
    expectNoColor("neither: the one in the middle", "c2");
    expectColor(":only-child", "d1", 0x0D0001);
    expectColor("p:first-of-type: after an h3", "p1", 0x0E0001);
    expectColor("p:last-of-type", "p3", 0x0E0002);
    expectColor("h3:nth-of-type(2)", "h2", 0x0E0003);
    expectColor(":nth-child(3) counts every element", "p2", 0x0E0004);
    expectColor("li:first-child label: a place up the tree", "el1", 0x0F0001);
    expectNoColor("li:first-child label: not in the 2nd", "el2");
    expectColor(".sp:first-child beats li.sp: a class more", "f1", 0x070001);
    expectColor("li.sp for the 2nd", "f2", 0x070002);
    expectColor(":nth-child(foo) drops out, the list stays", "ok2", 0x070003);
    {
        bool none = true;
        for (const char* id : {"b1", "b2", "b3", "b4", "b5", "b6", "b7"}) {
            const MW* w = byId(id);
            none = none && w && !(w->hasColor && w->color == 0x0B0003);
        }
        expectTrue(":nth-child(2n+9) of 7: none", none);
    }

    printf("\nSiblings:\n");
    expectColor(".a + .b: the next one", "s2", 0x050001);
    expectTrue(".a + .b: not a later one", byId("s4") && !(byId("s4")->hasColor && byId("s4")->color == 0x050001),
               colorOf(byId("s4")));
    {
        const MW* s2 = byId("s2");
        const MW* s4 = byId("s4");
        expectTrue(".a ~ .b: every later one", s2 && s4 && s2->borderWidth == 1 && s4->borderWidth == 1 &&
                                                   s4->borderColor == 0x050002,
                   s4 ? "border " + std::to_string(s4->borderWidth) : "no widget");
        expectTrue(".c + .b", s4 && s4->textDecor == LV_TEXT_DECOR_UNDERLINE,
                   s4 ? "decor " + std::to_string(s4->textDecor) : "no widget");
        expectTrue("label:last-child", s4 && s4->letterSpace == 2 && byId("s3") && byId("s3")->letterSpace != 2,
                   s4 ? "letter space " + std::to_string(s4->letterSpace) : "no widget");
    }
    expectNoColor(".b + .a: the order counts", "s1");
    expectColor("#t1 + p: text and comments between do not count", "t2", 0x060001);
    {
        const MW* t3 = byId("t3");
        expectTrue("#t1 ~ p: the one after that too", t3 && t3->letterSpace == 3 && !(t3->hasColor && t3->color == 0x060001),
                   t3 ? "letter space " + std::to_string(t3->letterSpace) + ", " + colorOf(t3) : "no widget");
    }

    printf("\nOther boards (the query alone):\n");
    {
        const UI::CssScreen wide{800, 480, 130};  // esp8048w550
        const UI::CssScreen small{240, 240, 130};  // twatch2020
        expectTrue("800x480: (orientation: landscape)", UI::cssMediaMatches("(orientation: landscape)", wide));
        expectTrue("800x480: (min-width: 600px) and (max-height: 500px)",
                   UI::cssMediaMatches("(min-width: 600px) and (max-height: 500px)", wide));
        expectTrue("800x480: not (max-aspect-ratio: 1/1)", UI::cssMediaMatches("not (max-aspect-ratio: 1/1)", wide));
        expectTrue("240x240: (max-width: 240px)", UI::cssMediaMatches("(max-width: 240px)", small));
        expectTrue("240x240: (width < 300px) or (hover)", UI::cssMediaMatches("(width < 300px) or (hover)", small));
        expectTrue("480x480: not (orientation: landscape)",
                   UI::cssMediaMatches("not (orientation: landscape)", UI::CssScreen{480, 480, 130}));
    }

    printf("\nA page built from a copy (@ in it):\n");
    expectColor("li:last-child", "z2", 0x090001);
    expectNoColor("li:last-child: not the 1st", "z1");
    expectColor(".mail + p", "z0", 0x090002);

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS STRUCTURE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS STRUCTURE TESTS PASSED ===\n", passed, total);
    return 1;
}
