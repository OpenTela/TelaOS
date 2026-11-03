/**
 * Test: CSS selectors and cascade — lists (h1, h2), descendant and child combinators, *,
 * attribute selectors, the page as an ancestor, <label> and <button> as ancestors of their HTML,
 * specificity, !important and style="", a run of text.
 * The simple selectors and their old cascade are in test_css.cpp. See docs/CSS.md.
 */
#include <cstdio>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"

using MW = LvglMock::Widget;

static const char* APP = R"HTML(
<app>
  <style>
    h1, h2 { color: #111111; }
    nav a { color: #222222; }
    .menu > li { color: #333333; }
    ul li li { color: #EEEEEE; }
    .card label { color: #444444; }
    a:hover, .ok { color: #555555; }
    [data-tone="warn"] { color: #666666; }
    a[href^="http"] { color: #777777; }
    [lang|="en"] { color: #0B0B0B; }
    #main .big { color: #888888; }
    #other .big { color: #FF0000; }
    p.note { color: #999999; }
    .note { color: #FF0000; }
    .must { color: #AAAAAA !important; }
    #strong { color: #FF0000; }
    .gone { display: none; }
    input[type="checkbox"] { background: #BBBBBB; }
    div > label.direct { color: #CCCCCC; }
    .uni > * { color: #0A0A0A; }
    /* a comment between rules */ .commented { color: #123456; /* and inside */ }
    .quoted { font-family: "a;b"; color: #234567; }
    .dbl { color: #135790;; }
    .nocolon { stray; color: #246801; }
    .imp { color: #345678 !important; }
    #inl { color: #FF0000; }
    #runp { color: #0D0D0D; }
    #runp * { color: #FF0000; }
    .lx i { color: #0E0E0E; }
    .bx b { color: #0F0F0F; }
  </style>
  <state>
    <int name="n" default="7"/>
  </state>
  <ui default="/main">
    <page id="main">
      <h1 id="h1">One</h1>
      <h2 id="h2">Two</h2>
      <nav><p id="navp"><a href="/x">Home</a> <a href="/y">Next</a></p></nav>
      <ul class="menu"><li id="li1">Direct<ul><li id="li2">Grand</ul></ul>
      <div class="card"><label id="cardlbl">In card</label></div>
      <label id="outlbl">Outside</label>
      <p class="ok" id="ok">ok</p>
      <p data-tone="warn" id="warn">warn</p>
      <p id="ext"><a href="https://e.com">E</a> <a href="/z">L</a></p>
      <p lang="en-US" id="lang">en-US</p>
      <p class="big" id="big">big</p>
      <p class="note" id="note">note</p>
      <p class="must" id="strong">must</p>
      <p class="gone" id="gone">gone</p>
      <p><input type="checkbox" id="cb"> box</p>
      <div><label class="direct" id="direct">direct</label></div>
      <div class="uni"><label id="uni1">any child</label></div>
      <p class="commented" id="commented">c</p>
      <p class="quoted" id="quoted">q</p>
      <p class="dbl" id="dbl">;;</p>
      <p class="nocolon" id="nocolon">no colon</p>
      <p class="imp" id="imp" style="color: #FF0000">sheet !important</p>
      <p id="inl" style="color: #456789">inline</p>
      <p id="inlimp" class="must" style="color: #567890 !important">inline !important</p>
      <p id="runp">n {n} <a href="/x">a</a> <a href="/y">b</a></p>
      <label class="lx" id="lx">E=mc<sup>2</sup> <i>opt</i></label>
      <button class="bx" id="bx"><b>Save</b> x<sup>2</sup></button>
    </page>
    <page id="other">
      <p class="big" id="big2">big on other</p>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

// A label straight under w with this text (a word of an HTML line)
static const MW* word(const MW* w, const char* text) {
    if (!w) return nullptr;
    for (const MW* c : w->children) {
        if (c->type == "Label" && c->text == text) return c;
    }
    return nullptr;
}

// A label anywhere under w whose text, without the spaces around it, is text
static const MW* deepWord(const MW* w, const char* text) {
    if (!w) return nullptr;
    for (const MW* c : w->children) {
        if (c->type == "Label") {
            const std::string& t = c->text;
            const size_t b = t.find_first_not_of(' '), e = t.find_last_not_of(' ');
            if (b != std::string::npos && t.compare(b, e - b + 1, text) == 0) return c;
        }
        if (const MW* f = deepWord(c, text)) return f;
    }
    return nullptr;
}

static std::string hex(uint32_t c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%06X", c);
    return buf;
}

static void expectColor(const char* name, const MW* w, uint32_t want) {
    TEST(name) {
        if (w && w->hasColor && w->color == want) PASS();
        else FAIL(w ? ("color " + (w->hasColor ? hex(w->color) : std::string("not set"))).c_str() : "no widget");
    }
}

static void expectTrue(const char* name, bool ok, const char* why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why);
    }
}

int main() {
    printf("=== CSS SELECTORS AND CASCADE ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("Selectors:\n");
    expectColor("h1, h2: a list (h1)", byId("h1"), 0x111111);
    expectColor("h1, h2: a list (h2)", byId("h2"), 0x111111);
    expectColor("nav a: a link inside <nav>", word(byId("navp"), "Home"), 0x222222);
    expectColor("nav a: the second link too", word(byId("navp"), "Next"), 0x222222);
    expectColor(".menu > li: a direct child", byId("li1"), 0x333333);
    expectColor("ul li li: a nested item (not .menu > li)", byId("li2"), 0xEEEEEE);
    expectColor(".card label: a TelaOS label inside a TelaOS div", byId("cardlbl"), 0x444444);
    expectTrue(".card label: not a label outside it", byId("outlbl") && byId("outlbl")->color != 0x444444);
    expectColor("div > label.direct: the child combinator on TelaOS", byId("direct"), 0xCCCCCC);
    expectColor(".uni > *: any child", byId("uni1"), 0x0A0A0A);
    expectColor("a:hover, .ok: the readable part of a list stays", byId("ok"), 0x555555);
    expectColor("[data-tone=\"warn\"]", byId("warn"), 0x666666);
    expectColor("a[href^=\"http\"]: the external link", word(byId("ext"), "E"), 0x777777);
    expectTrue("a[href^=\"http\"]: not the local one", word(byId("ext"), "L") && word(byId("ext"), "L")->color != 0x777777);
    expectColor("[lang|=\"en\"]: en-US", byId("lang"), 0x0B0B0B);
    {
        const MW* cb = byId("cb");
        expectTrue("input[type=\"checkbox\"]", cb && cb->hasBgcolor && cb->bgcolor == 0xBBBBBB,
                   cb ? ("bgcolor " + hex(cb->bgcolor)).c_str() : "no widget");
    }
    expectColor("#main .big: the page is an ancestor", byId("big"), 0x888888);
    expectColor(".lx i: a <label> is an ancestor of its HTML", deepWord(byId("lx"), "opt"), 0x0E0E0E);
    expectColor(".bx b: a <button> is an ancestor of its HTML", deepWord(byId("bx"), "Save"), 0x0F0F0F);
    expectColor("#other .big: on the other page", byId("big2"), 0xFF0000);

    printf("\nCascade:\n");
    expectColor("p.note beats .note, though .note comes later", byId("note"), 0x999999);
    expectColor("!important beats #id", byId("strong"), 0xAAAAAA);
    expectColor("style=\"\" beats #id", byId("inl"), 0x456789);
    expectColor("!important in a sheet beats style=\"\"", byId("imp"), 0x345678);
    expectColor("style=\"... !important\" beats !important in a sheet", byId("inlimp"), 0x567890);
    expectTrue("display: none from a style sheet", byId("gone") && byId("gone")->hidden);
    {  // a run of text is not an element: #runp * styles the links, not the live word {n}
        const MW* plain = word(byId("runp"), "n ");
        const MW* live = word(byId("runp"), "7 ");
        expectTrue("#runp *: a live word has its paragraph's color, as the word before it",
                   plain && live && live->hasColor == plain->hasColor && live->color == plain->color,
                   live ? ("color " + (live->hasColor ? hex(live->color) : std::string("not set"))).c_str() : "no widget");
    }
    expectColor("#runp *: a link inside is an element", word(byId("runp"), "a"), 0xFF0000);

    printf("\nParsing:\n");
    expectColor("comments between rules and inside a block", byId("commented"), 0x123456);
    expectColor("';' inside quotes does not end a declaration", byId("quoted"), 0x234567);
    // the old parser looped forever on these: the watch froze on a sheet with a typo
    expectColor("';;' in a block", byId("dbl"), 0x135790);
    expectColor("a declaration without ':'", byId("nocolon"), 0x246801);

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS SELECTOR TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS SELECTOR TESTS PASSED ===\n", passed, total);
    return 1;
}
