/**
 * Test: CSS states and motion — :active, :focus, :checked, :disabled, :hover as LVGL state
 * styles, transform, transition; what sticks out of HTML blocks and lines (a ring, a shadow).
 * See docs/CSS.md.
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
    .b { color: #111111; }
    .b:active { color: #FF0000; }
    .f:focus { color: #00AA00; }
    .ck:checked { background: #00FF00; }
    .dis:disabled { opacity: 0.5; }
    .hv:hover { color: #0000AA; }
    .l, .l:active { color: #222222; }
    .s:active { color: #AA0000; }
    #sid:active { color: #BB0000; }
    .w:active { width: 300px; color: #CC0000; }
    .p:active .c { color: #FF00FF; }
    .v { --c: #0E0E0E; }
    .v:active { color: var(--c); }
    .bd:active { border: 2px solid #123123; }
    .t { transform: translate(10px, 5px) scale(1.5) rotate(90deg); }
    .ts:active { transform: scale(0.9); }
    .tk { position: relative; left: 7px; transform: rotate(10deg); }
    .tskew { transform: skew(10deg); }
    .tr { transition: background-color 0.3s ease; }
    .tr:active { background: #333333; }
    .tall { transition: all 200ms linear 50ms; }
    .tall:active { color: #444444; }
    .tr2 { transition: opacity 1s; }
    .tnone { transition: none; }
    .tnone:active { color: #555555; }
    .run { color: #010101; }
    .run:active { color: #020202; }
    .ring:checked { outline: 2px solid #00FF00; }
    .cut { overflow: hidden; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="b" id="b">b</label>
      <label class="f" id="f">f</label>
      <p><input type="checkbox" class="ck" id="ck"> box</p>
      <label class="dis" id="dis">dis</label>
      <label class="hv" id="hv">hv</label>
      <label class="l" id="l">l</label>
      <label class="s" id="sid">s</label>
      <label class="w" id="w">w</label>
      <div class="p"><label class="c" id="c">c</label></div>
      <label class="v" id="v">v</label>
      <label class="bd" id="bd">bd</label>
      <label class="t" id="t">t</label>
      <label class="ts" id="ts">ts</label>
      <label class="tk" id="tk">tk</label>
      <label class="tskew" id="tskew">tskew</label>
      <label class="tr" id="tr">tr</label>
      <label class="tall" id="tall">tall</label>
      <label class="tr2" id="tr2">tr2</label>
      <label class="tnone" id="tnone">tnone</label>
      <p id="runs">x <a href="/a">a</a> <a href="/b">b</a> <span class="run">r</span></p>
      <p id="line"><input type="checkbox" class="ring" id="cbx"> text</p>
      <p><input type="radio" name="g" id="rad"> radio</p>
      <section id="sec"><p>a block</p></section>
      <section class="cut" id="seccut"><p>clipped</p></section>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

static const MW* word(const MW* w, const char* text) {
    if (!w) return nullptr;
    for (const MW* c : w->children) {
        if (c->type == "Label" && c->text == text) return c;
    }
    return nullptr;
}

static std::string hex(long long c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%06llX", c);
    return buf;
}

static void expectTrue(const char* name, bool ok, const std::string& why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why.c_str());
    }
}

// The value a state style has: prop for LV_PART_MAIN | state
static void expectState(const char* name, const char* id, const char* prop, int state, long long want) {
    const MW* w = byId(id);
    const MW::StateStyle* st = w ? w->stateStyle(prop, LV_PART_MAIN | state) : nullptr;
    expectTrue(name, st && st->value == want,
               !w ? "no widget" : st ? "got " + hex(st->value) + " (" + std::to_string(st->value) + ")" : "no state style");
}

static void expectNoState(const char* name, const char* id) {
    const MW* w = byId(id);
    expectTrue(name, w && w->stateStyles.empty(), w ? std::to_string(w->stateStyles.size()) + " state styles" : "no widget");
}

int main() {
    printf("=== CSS STATES AND MOTION ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("States:\n");
    {
        const MW* w = byId("b");
        expectTrue(":active: the element as it is keeps its color", w && w->hasColor && w->color == 0x111111,
                   w ? "color " + hex(w->color) : "no widget");
    }
    expectState(":active: the pressed color", "b", "text_color", LV_STATE_PRESSED, 0xFF0000);
    expectState(":focus", "f", "text_color", LV_STATE_FOCUSED, 0x00AA00);
    expectState(":checked on a checkbox", "ck", "bg_color", LV_STATE_CHECKED, 0x00FF00);
    expectState(":disabled { opacity: 0.5 }", "dis", "opa", LV_STATE_DISABLED, 128);
    expectState(":hover is a tap on a touch screen", "hv", "text_color", LV_STATE_PRESSED, 0x0000AA);
    expectState(".l, .l:active: the list, for the pressed state too", "l", "text_color", LV_STATE_PRESSED, 0x222222);
    expectState("#sid:active beats .s:active", "sid", "text_color", LV_STATE_PRESSED, 0xBB0000);
    {
        const MW* w = byId("w");
        expectTrue("a state takes the look, not the size", w && w->w != 300 && w->stateStyle("text_color", LV_STATE_PRESSED),
                   w ? "w " + std::to_string(w->w) : "no widget");
    }
    {
        const MW* w = byId("c");
        expectTrue(".p:active .c: left out (an ancestor's state)", w && !w->hasColor && w->stateStyles.empty(),
                   w ? (w->hasColor ? "color " + hex(w->color) : std::to_string(w->stateStyles.size()) + " state styles")
                     : "no widget");
    }
    expectState("var() in a state sees the element's own --c", "v", "text_color", LV_STATE_PRESSED, 0x0E0E0E);
    expectState(":active { border }: the width", "bd", "border_width", LV_STATE_PRESSED, 2);
    expectState(":active { border }: the color", "bd", "border_color", LV_STATE_PRESSED, 0x123123);

    printf("\ntransform:\n");
    {
        const MW* w = byId("t");
        expectTrue("translate(10px, 5px) scale(1.5) rotate(90deg)",
                   w && w->translateX == 10 && w->translateY == 5 && w->scaleX == 384 && w->scaleY == 384 &&
                       w->rotation == 900 && w->pivotX == lv_pct(50) && w->pivotY == lv_pct(50),
                   w ? std::to_string(w->translateX) + "," + std::to_string(w->translateY) + " scale " +
                           std::to_string(w->scaleX) + " rotation " + std::to_string(w->rotation)
                     : "no widget");
        w = byId("tk");
        expectTrue("rotate() keeps a position: relative shift", w && w->translateX == 7 && w->rotation == 100,
                   w ? std::to_string(w->translateX) + " rotation " + std::to_string(w->rotation) : "no widget");
        w = byId("tskew");
        expectTrue("skew(): left out", w && w->scaleX == 256 && w->rotation == 0,
                   w ? "scale " + std::to_string(w->scaleX) : "no widget");
    }
    expectState(":active { transform: scale(0.9) }", "ts", "transform_scale_x", LV_STATE_PRESSED, 230);

    printf("\ntransition:\n");
    {
        const MW* w = byId("tr");
        const bool ok = w && w->transitions.size() == 2 && w->transitions[0].selector == LV_PART_MAIN &&
                        w->transitions[0].time == 300 && w->transitions[0].props == 2 &&
                        w->transitions[1].selector == (LV_PART_MAIN | LV_STATE_PRESSED) && w->transitions[1].time == 300;
        expectTrue("background-color 0.3s: back from :active and into it", ok,
                   w ? std::to_string(w->transitions.size()) + " transitions" : "no widget");
        w = byId("tall");
        expectTrue("all 200ms linear 50ms", w && !w->transitions.empty() && w->transitions[0].time == 200 &&
                                                w->transitions[0].delay == 50 && w->transitions[0].props == 23,
                   w && !w->transitions.empty() ? std::to_string(w->transitions[0].time) + "ms, " +
                                                      std::to_string(w->transitions[0].props) + " properties"
                                                : "none");
        w = byId("tr2");
        expectTrue("no state rules: the element's own only", w && w->transitions.size() == 1 && w->transitions[0].time == 1000,
                   w ? std::to_string(w->transitions.size()) + " transitions" : "no widget");
        w = byId("tnone");
        expectTrue("transition: none", w && w->transitions.empty(), w ? std::to_string(w->transitions.size()) : "no widget");
    }

    printf("\nText runs:\n");
    {
        const MW* r = word(byId("runs"), "r");
        expectTrue("a <span> has no LVGL state: it keeps its color", r && r->hasColor && r->color == 0x010101,
                   r ? "color " + hex(r->color) : "no widget");
    }
    expectNoState("an element without state rules has no state styles", "tskew");

    printf("\nWhat sticks out (a ring, a shadow):\n");
    {
        const MW* w = byId("line");
        expectTrue("a line of text does not clip it, as in HTML", w && w->overflowVisible, w ? "clipped" : "no widget");
        w = byId("sec");
        expectTrue("a block does not clip it either", w && w->overflowVisible, w ? "clipped" : "no widget");
        w = byId("seccut");
        expectTrue("overflow: hidden clips it", w && !w->overflowVisible, w ? "not clipped" : "no widget");
        w = byId("cbx");
        expectTrue("a checkbox is its box: the gap to the text is outside", w && w->padColumn == 0 && w->marginRight == 8,
                   w ? "gap " + std::to_string(w->padColumn) + ", margin " + std::to_string(w->marginRight) : "no widget");
        w = byId("rad");
        expectTrue("a radio's ring is round", w && w->radius == LV_RADIUS_CIRCLE,
                   w ? "radius " + std::to_string(w->radius) : "no widget");
    }
    expectState(":checked { outline }: round the box", "cbx", "outline_width", LV_STATE_CHECKED, 2);

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS STATE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS STATE TESTS PASSED ===\n", passed, total);
    return 1;
}
