/**
 * Test: CSS layout — flex containers (display: flex, flex-direction, flex-wrap, flex-flow,
 * justify-content, align-items, align-content), flex items (flex, flex-grow), position with
 * left/top/right/bottom, and visibility. See docs/CSS.md.
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
    .row { display: flex; }
    .col { display: flex; flex-direction: column; }
    .wrapr { display: flex; flex-flow: row-reverse wrap; }
    .tocol { flex-direction: column; }
    .place { display: flex; justify-content: space-between; align-items: center; align-content: center; }
    .stretch { display: flex; align-items: stretch; }
    .center { display: flex; flex-direction: column; align-items: center; }
    .jc { justify-content: center; }
    .grow { flex: 1; }
    .grow2 { flex-grow: 2; }
    .nogrow { flex: none; }
    .basis { flex: 100px; }
    .rel { position: relative; left: 10px; top: 5px; }
    .relrb { position: relative; right: 8px; bottom: 4px; }
    .abs { position: absolute; right: 10px; bottom: 20px; }
    .fixed { position: fixed; top: 0; left: 0; }
    .static { left: 12px; top: 7px; }
    .vis { visibility: hidden; }
  </style>
  <ui default="/main">
    <page id="main">
      <div class="row" id="row"><label>a</label></div>
      <div class="col" id="col"><label>a</label></div>
      <div class="wrapr" id="wrapr"><label>a</label></div>
      <div flex="row" class="tocol" id="tocol"><label>a</label></div>
      <div class="tocol" id="notflex"><label>a</label></div>
      <div class="place" id="place"><label>a</label></div>
      <div class="stretch" id="stretch"><label>a</label></div>
      <div class="center" id="center"><label>a</label></div>
      <section class="jc" id="jc"><p>a</p></section>
      <div flex="row" w="200">
        <label class="grow" id="grow">a</label>
        <label class="grow2" id="grow2">b</label>
        <label class="nogrow" id="nogrow">c</label>
        <label class="basis" id="basis">d</label>
      </div>
      <section><p class="grow" id="incol">no room to grow</p></section>
      <label class="rel" id="rel">rel</label>
      <label class="relrb" id="relrb">relrb</label>
      <label class="abs" id="abs">abs</label>
      <label class="fixed" id="fixed">fixed</label>
      <label class="static" id="static">static</label>
      <label class="vis" id="vis">hidden</label>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

static void expectTrue(const char* name, bool ok, const std::string& why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why.c_str());
    }
}

static std::string flex(const MW* w) {
    if (!w) return "no widget";
    return "flow " + std::to_string(w->flexFlow) + ", place " + std::to_string(w->flexMainAlign) + " " +
           std::to_string(w->flexCrossAlign) + " " + std::to_string(w->flexTrackAlign) + ", grow " +
           std::to_string(w->flexGrow);
}

static void expectFlow(const char* name, const char* id, int flow) {
    const MW* w = byId(id);
    expectTrue(name, w && w->flexFlow == flow, flex(w));
}

static void expectGrow(const char* name, const char* id, int grow) {
    const MW* w = byId(id);
    expectTrue(name, w && w->flexGrow == grow, flex(w));
}

static std::string place(const MW* w) {
    if (!w) return "no widget";
    return "x " + std::to_string(w->x) + " y " + std::to_string(w->y) + ", translate " + std::to_string(w->translateX) +
           " " + std::to_string(w->translateY) + ", align " + std::to_string(w->alignType) + " " +
           std::to_string(w->alignOfsX) + " " + std::to_string(w->alignOfsY) + (w->ignoreLayout ? ", out of layout" : "") +
           (w->floating ? ", floating" : "");
}

int main() {
    printf("=== CSS LAYOUT ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("Flex container:\n");
    expectFlow("display: flex: a row", "row", LV_FLEX_FLOW_ROW);
    expectFlow("display: flex; flex-direction: column", "col", LV_FLEX_FLOW_COLUMN);
    expectFlow("flex-flow: row-reverse wrap", "wrapr", LV_FLEX_FLOW_ROW_WRAP_REVERSE);
    expectFlow("flex-direction: column on <div flex=\"row\">", "tocol", LV_FLEX_FLOW_COLUMN);
    expectFlow("flex-direction without flex: not a flex container", "notflex", -1);
    {
        const MW* w = byId("place");
        expectTrue("justify-content: space-between, align-items: center",
                   w && w->flexMainAlign == LV_FLEX_ALIGN_SPACE_BETWEEN && w->flexCrossAlign == LV_FLEX_ALIGN_CENTER,
                   flex(w));
        expectTrue("align-content: center", w && w->flexTrackAlign == LV_FLEX_ALIGN_CENTER, flex(w));
        w = byId("stretch");
        expectTrue("align-items: stretch: the start (LVGL has no stretch)",
                   w && w->flexCrossAlign == LV_FLEX_ALIGN_START && w->flexFlow == LV_FLEX_FLOW_ROW, flex(w));
        w = byId("center");
        expectTrue("align-items: center in one line: the line is centered too",
                   w && w->flexCrossAlign == LV_FLEX_ALIGN_CENTER && w->flexTrackAlign == LV_FLEX_ALIGN_CENTER, flex(w));
        w = byId("jc");
        expectTrue("justify-content on a block of the HTML flow (a column)",
                   w && w->flexFlow == LV_FLEX_FLOW_COLUMN && w->flexMainAlign == LV_FLEX_ALIGN_CENTER, flex(w));
    }

    printf("\nFlex item:\n");
    expectGrow("flex: 1", "grow", 1);
    expectGrow("flex-grow: 2", "grow2", 2);
    expectGrow("flex: none", "nogrow", 0);
    expectGrow("flex: 100px is 1 1 100px", "basis", 1);
    expectGrow("flex: 1 in a column sized by its content: no room", "incol", 0);

    printf("\nPosition:\n");
    {
        const MW* w = byId("rel");
        expectTrue("position: relative; left: 10px; top: 5px: moved, not placed",
                   w && w->translateX == 10 && w->translateY == 5 && !w->ignoreLayout, place(w));
        w = byId("relrb");
        expectTrue("position: relative; right: 8px; bottom: 4px", w && w->translateX == -8 && w->translateY == -4, place(w));
        w = byId("abs");
        expectTrue("position: absolute; right: 10px; bottom: 20px",
                   w && w->ignoreLayout && w->alignType == LV_ALIGN_BOTTOM_RIGHT && w->alignOfsX == -10 &&
                       w->alignOfsY == -20,
                   place(w));
        w = byId("fixed");
        expectTrue("position: fixed; top: 0; left: 0: out of scrolling too",
                   w && w->floating && w->alignType == LV_ALIGN_TOP_LEFT && w->alignOfsX == 0 && w->alignOfsY == 0, place(w));
        w = byId("static");
        expectTrue("left and top without position: placed, as before", w && w->x == 12 && w->y == 7, place(w));
    }

    printf("\nvisibility:\n");
    {
        const MW* w = byId("vis");
        expectTrue("visibility: hidden: not seen, keeps its place", w && w->opacity == 0 && !w->hidden,
                   w ? "opacity " + std::to_string(w->opacity) + (w->hidden ? ", hidden" : "") : "no widget");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS LAYOUT TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS LAYOUT TESTS PASSED ===\n", passed, total);
    return 1;
}
