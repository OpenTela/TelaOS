/**
 * Test: CSS :not(), :is(), :where() with selector lists, and ::placeholder of a text field.
 * See docs/CSS.md.
 */
#include <cstdio>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
#include "ui/css_parser.h"

using MW = LvglMock::Widget;

static const char* APP = R"HTML(
<app>
  <style>
    .n:not(.x) { color: #010001; }
    #ln li:not(:last-child) { color: #010002; }
    .i:is(.a, .b) { color: #010003; }
    .s1:is(#sp, .q) { color: #010004; }
    .s1.q.r { color: #010005; }
    label { color: #010007; }
    :where(#wid) { color: #010006; }
    #cx label:not(.box label) { color: #010009; }
    .v:not(:hover), .ok3 { color: #01000A; }
    .f:is(:hover, .yes) { color: #010008; }
    :is(section, article) .inp { color: #01000B; }
    .k:not(:is(.a, .b)) { color: #01000C; }
    .m:not(.a, .b) { color: #01000D; }
    [data-t="a, b"] { color: #01000E; }

    .ph::placeholder { color: #0A0A0A; letter-spacing: 2px; }
    .ph2::placeholder { color: rgba(255, 0, 0, 0.5); }
    .ph3::placeholder { opacity: 0.5; }
    .ph4::placeholder:hover { color: #0B0B0B; }
    .ph5:focus::placeholder { color: #0C0C0C; }
    .lab::placeholder { color: #0D0D0D; }

    .dyn:active { color: #0E0001; }
    .dyn2:first-child { color: #0E0003; }
    .dyn2 { color: #0E0002; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="n" id="n1">n</label>
      <label class="n x" id="n2">n x</label>
      <ul id="ln"><li id="l1">1</li><li id="l2">2</li><li id="l3">3</li></ul>
      <label class="i a" id="i1">i a</label>
      <label class="i b" id="i2">i b</label>
      <label class="i" id="i3">i</label>
      <label class="s1 q r" id="sp">s1 q r</label>
      <label id="wid">where</label>
      <div id="cx">
        <label id="c1">outside</label>
        <div class="box"><label id="c2">inside</label></div>
      </div>
      <label class="v" id="v1">v</label>
      <label class="ok3" id="ok3">ok3</label>
      <label class="f yes" id="f1">f yes</label>
      <section><p class="inp" id="s2">in a section</p></section>
      <label class="k" id="k1">k</label>
      <label class="k a" id="k2">k a</label>
      <label class="m" id="m1">m</label>
      <label class="m b" id="m2">m b</label>
      <label data-t="a, b" id="dt">a comma in an attribute</label>

      <input class="ph" id="in1" placeholder="name">
      <input class="ph2" id="in2" placeholder="name">
      <input class="ph3" id="in3" placeholder="name">
      <input class="ph4" id="in4" placeholder="name">
      <input class="ph5" id="in5" placeholder="name">
      <label class="lab" id="lab">a label has no placeholder</label>
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

static void expectNotColor(const char* name, const char* id, uint32_t unwanted) {
    const MW* w = byId(id);
    expectTrue(name, w && !(w->hasColor && w->color == unwanted), colorOf(w));
}

// The value a placeholder style has
static const MW::StateStyle* hint(const char* id, const char* prop) {
    const MW* w = byId(id);
    return w ? w->stateStyle(prop, LV_PART_TEXTAREA_PLACEHOLDER) : nullptr;
}

int main() {
    printf("=== CSS :NOT(), :IS(), :WHERE(), ::PLACEHOLDER ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf(":not(), :is(), :where():\n");
    expectColor(".n:not(.x)", "n1", 0x010001);
    expectNotColor(".n:not(.x): not with .x", "n2", 0x010001);
    expectColor("li:not(:last-child): the first", "l1", 0x010002);
    expectNotColor("li:not(:last-child): not the last", "l3", 0x010002);
    expectColor(".i:is(.a, .b) with .a", "i1", 0x010003);
    expectColor(".i:is(.a, .b) with .b", "i2", 0x010003);
    expectNotColor(".i:is(.a, .b): neither", "i3", 0x010003);
    expectColor(":is(#sp, .q) counts as an id: beats .s1.q.r after it", "sp", 0x010004);
    expectColor(":where(#wid) counts nothing: label {} wins", "wid", 0x010007);
    expectColor(":not(.box label): outside .box", "c1", 0x010009);
    expectNotColor(":not(.box label): not inside it", "c2", 0x010009);
    expectNotColor(":not(:hover): a state inside, left out", "v1", 0x01000A);
    expectColor("... and the rest of the list stays", "ok3", 0x01000A);
    expectColor(":is(:hover, .yes): the state dropped, .yes stays", "f1", 0x010008);
    expectColor(":is(section, article) .inp", "s2", 0x01000B);
    expectColor(".k:not(:is(.a, .b))", "k1", 0x01000C);
    expectNotColor(".k:not(:is(.a, .b)): not with .a", "k2", 0x01000C);
    expectColor(".m:not(.a, .b): a list in :not()", "m1", 0x01000D);
    expectNotColor(".m:not(.a, .b): not with .b", "m2", 0x01000D);
    expectColor("[data-t=\"a, b\"]: the list splits outside quotes", "dt", 0x01000E);

    printf("\n::placeholder:\n");
    {
        const MW::StateStyle* c = hint("in1", "text_color");
        expectTrue("color", c && c->value == 0x0A0A0A, c ? hex(c->value) : "no style");
        const MW::StateStyle* ls = hint("in1", "text_letter_space");
        expectTrue("letter-spacing", ls && ls->value == 2, ls ? std::to_string(ls->value) : "no style");
        const MW* w = byId("in1");
        expectTrue("the field itself keeps its color", w && !(w->hasColor && w->color == 0x0A0A0A), colorOf(w));
        const MW::StateStyle* a = hint("in2", "text_opa");
        expectTrue("rgba(): its alpha", a && a->value == 128, a ? std::to_string(a->value) : "no style");
        const MW::StateStyle* o = hint("in3", "text_opa");
        expectTrue("opacity: 0.5", o && o->value == 128, o ? std::to_string(o->value) : "no style");
        expectTrue("::placeholder:hover: left out", !hint("in4", "text_color"));
        expectTrue(":focus::placeholder: left out", !hint("in5", "text_color"));
        const MW* l = byId("lab");
        expectTrue("a label: no placeholder style", l && !l->stateStyle("text_color", LV_PART_TEXTAREA_PLACEHOLDER) &&
                                                       !(l->hasColor && l->color == 0x0D0D0D),
                   colorOf(l));
    }

    printf("\nThe class-only lookup (class=\"{var}\" on a button):\n");
    expectTrue(".dyn:active is not .dyn", UI::Css::instance().prop("dyn", "color").empty(),
               UI::Css::instance().prop("dyn", "color").c_str());
    expectTrue(".dyn2:first-child is not .dyn2", UI::Css::instance().prop("dyn2", "color") == "#0E0002",
               UI::Css::instance().prop("dyn2", "color").c_str());

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS PSEUDO TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS PSEUDO TESTS PASSED ===\n", passed, total);
    return 1;
}
