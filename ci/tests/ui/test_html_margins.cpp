/**
 * Test: the margins of HTML blocks, as a browser has them (docs/HTML_TAGS.md, "Отступы"): its
 * style sheet's (<p> 1em, <h1> 0.67em of its own font, a list in a list none), the page's CSS
 * instead, collapsing between blocks, out of a parent with nothing in their way, into the body's
 * 8px. The objects carry the used margins: flex adds the one below to the next one's above.
 * And aside: 40px on both sides of <blockquote> and <figure>, on the side lines start of <dd>,
 * <fieldset>'s 2px with its frame and padding; the page's CSS instead.
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
    .m { margin: 5px; }
  </style>
  <ui default="/main">
    <page id="main">
      <p id="p1">One</p>
      <p id="p2">Two</p>
      <h1 id="h1">Big</h1>
      <p id="afterH1">After</p>
      <section id="wrap"><p id="inWrap">Inside</p></section>
      <section id="padded" style="padding-top: 4px"><p id="inPadded">Inside</p></section>
      <p id="zero" style="margin: 0">No margin</p>
      <p id="cssTop" style="margin-top: 30px">Thirty</p>
      <ul id="outer"><li>a<ul id="inner"><li>b</li></ul></li></ul>
      <ul id="pl"><li id="lp"><p id="inLi">In an item</p></li></ul>
      <p id="beforeGrow">Before</p>
      <td id="cell">A cell out of its table: it grows</td>
      <label id="tela" class="m">TelaOS</label>
      <p id="last">Last</p>
    </page>
  </ui>
</app>
)HTML";

static const char* APP_ASIDE = R"HTML(
<app>
  <style>
    .flat { margin: 0; }
  </style>
  <ui default="/main">
    <page id="main">
      <blockquote id="bq">Quote</blockquote>
      <figure id="fig"><figcaption>Caption</figcaption></figure>
      <dl><dt>Term</dt><dd id="dd">Definition</dd></dl>
      <dl dir="rtl"><dt>Term</dt><dd id="ddRtl">Definition</dd></dl>
      <fieldset id="fs"><legend>Group</legend>Inside</fieldset>
      <blockquote id="flat" class="flat">The page's CSS</blockquote>
      <figure id="oneSide" style="margin-left: 0">The page's CSS, one side</figure>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static void render(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(html);
}

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

static std::string margins(const MW* w) {
    if (!w) return "no widget";
    return "margin " + std::to_string(w->marginTop) + " / " + std::to_string(w->marginBottom);
}

// The used margins of an element's object: above and below
static void expectMargins(const char* name, const char* id, int top, int bottom) {
    const MW* w = byId(id);
    TEST(name) {
        if (w && w->marginTop == top && w->marginBottom == bottom) PASS();
        else FAIL(margins(w).c_str());
    }
}

// An element's margins aside, its padding and frame: left, right
static void expectAside(const char* name, const char* id, int left, int right, int padLeft, int padRight,
                        int border) {
    const MW* w = byId(id);
    TEST(name) {
        if (w && w->marginLeft == left && w->marginRight == right && w->padLeft == padLeft &&
            w->padRight == padRight && w->borderWidth == border) {
            PASS();
        } else {
            FAIL(w ? ("margin " + std::to_string(w->marginLeft) + " / " + std::to_string(w->marginRight) +
                      ", pad " + std::to_string(w->padLeft) + " / " + std::to_string(w->padRight) + ", border " +
                      std::to_string(w->borderWidth)).c_str()
                   : "no widget");
        }
    }
}

int main() {
    printf("=== HTML MARGINS ===\n\n");
    render(APP);

    printf("between blocks:\n");
    expectMargins("<p> <p>: 16px between, the larger, not 32", "p2", 16, 0);
    expectMargins("<p>: its margin below is in the next one's", "p1", 0, 0);
    expectMargins("<h1> after <p>: 0.67em of 32px, 21 > 16", "h1", 21, 0);
    expectMargins("<p> after <h1>: 21, the heading's", "afterH1", 21, 0);

    printf("\nout of a parent:\n");
    expectMargins("<section><p>: the p's margins come out of it", "inWrap", 0, 0);
    expectMargins("...and are the section's", "wrap", 16, 0);
    expectMargins("padding-top: the p's above stays in", "inPadded", 16, 0);
    expectMargins("...its below comes out (no padding there)", "padded", 16, 0);

    printf("\nthe page's CSS:\n");
    expectMargins("margin: 0: none of its own; the section's 16 above", "zero", 16, 0);
    expectMargins("margin-top: 30px instead of 1em", "cssTop", 30, 0);

    printf("\nlists:\n");
    expectMargins("<ul>: 1em", "outer", 16, 0);
    expectMargins("a list in a list: none", "inner", 0, 0);
    expectMargins("<li><p>: the p's margins come out of the item", "inLi", 0, 0);
    expectMargins("...and out of the list, one with its 1em", "lp", 0, 0);
    expectMargins("...the list's: 16 between it and the list before", "pl", 16, 0);

    printf("\na box that grows (flex leaves its margins out of the room it shares):\n");
    expectMargins("the block before keeps its margin below", "beforeGrow", 16, 16);
    expectMargins("the growing one gets none of it", "cell", 0, 0);

    printf("\nTelaOS widgets and the body:\n");
    expectMargins("<label>: its CSS margins, as TelaOS lays it out", "tela", 5, 5);
    expectMargins("the last <p>: its margin below goes to the body", "last", 16, 0);
    {
        const MW* p1 = byId("p1");
        const MW* body = p1 ? p1->parent : nullptr;
        TEST("the body: 8px joins the first and last p's 16") {
            if (body && body->padTop == 16 && body->padBottom == 16) PASS();
            else FAIL(body ? ("pad " + std::to_string(body->padTop) + " / " + std::to_string(body->padBottom)).c_str()
                           : "no body");
        }
        TEST("nothing between blocks but their margins") {
            if (body && body->padRow == 0) PASS();
            else FAIL(body ? ("pad_row " + std::to_string(body->padRow)).c_str() : "no body");
        }
    }

    render(APP_ASIDE);
    printf("\naside, a browser's:\n");
    expectAside("<blockquote>: 40px on both sides, no line or padding", "bq", 40, 40, 0, 0, 0);
    expectAside("<figure>: 40px on both sides", "fig", 40, 40, 0, 0, 0);
    expectAside("<dd>: 40px on the side lines start", "dd", 40, 0, 0, 0, 0);
    expectAside("<dd> in dir=rtl: on the right", "ddRtl", 0, 40, 0, 0, 0);
    expectAside("<fieldset>: 2px, a 2px frame, 0.75em in it", "fs", 2, 2, 12, 12, 2);
    {
        const MW* w = byId("fs");
        TEST("<fieldset>: 0.35em above in it, 0.625em below") {
            if (w && w->padTop == 6 && w->padBottom == 10) PASS();
            else FAIL(w ? ("pad " + std::to_string(w->padTop) + " / " + std::to_string(w->padBottom)).c_str()
                        : "no widget");
        }
    }
    expectAside("the page's CSS instead: margin: 0", "flat", 0, 0, 0, 0, 0);
    expectAside("...margin-left: 0, the right one stays", "oneSide", 0, 40, 0, 0, 0);

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d HTML MARGINS TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d HTML MARGINS TESTS PASSED ===\n", passed, total);
    return 1;
}
