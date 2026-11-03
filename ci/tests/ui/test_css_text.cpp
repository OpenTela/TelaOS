/**
 * Test: CSS text — text-decoration on blocks and widgets, letter-spacing, line-height,
 * white-space: nowrap and text-overflow: ellipsis. See docs/CSS.md.
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
    .u { text-decoration: underline; }
    .lt { text-decoration: line-through red; }
    .both { text-decoration-line: underline line-through; }
    .nod { text-decoration: none; }
    .badd { text-decoration: blink; }
    .ls { letter-spacing: 2px; }
    .lsem { letter-spacing: 0.25em; }
    .lsdiv { letter-spacing: 3px; }
    .lh { line-height: 1.5; }
    .lhpx { line-height: 30px; }
    .lhpct { line-height: 200%; }
    .lhbig { font-size: 32px; line-height: 1.25; }
    .dots { width: 100px; white-space: nowrap; text-overflow: ellipsis; }
    .cut { width: 100px; white-space: nowrap; }
    .nw { white-space: nowrap; }
    .onlydots { width: 100px; text-overflow: ellipsis; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="u" id="u">u</label>
      <label class="lt" id="lt">lt</label>
      <label class="both" id="both">both</label>
      <label class="nod" id="nod">nod</label>
      <label class="badd" id="badd">badd</label>
      <p class="u" id="pu">a paragraph</p>
      <label class="ls" id="ls">ls</label>
      <label class="lsem" id="lsem">lsem</label>
      <div class="lsdiv" id="lsdiv"><label>inside</label></div>
      <label class="lh" id="lh">lh</label>
      <label class="lhpx" id="lhpx">lhpx</label>
      <label class="lhpct" id="lhpct">lhpct</label>
      <label class="lhbig" id="lhbig">lhbig</label>
      <label class="dots" id="dots">a long line that does not fit</label>
      <label class="cut" id="cut">a long line that does not fit</label>
      <label class="nw" id="nw">one line</label>
      <label class="onlydots" id="onlydots">text-overflow without nowrap</label>
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

static void expectInt(const char* name, const char* id, int MW::*field, int want) {
    const MW* w = byId(id);
    expectTrue(name, w && w->*field == want, w ? "got " + std::to_string(w->*field) : "no widget");
}

int main() {
    printf("=== CSS TEXT ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("text-decoration:\n");
    expectInt("text-decoration: underline", "u", &MW::textDecor, LV_TEXT_DECOR_UNDERLINE);
    expectInt("line-through red: the line, not its color", "lt", &MW::textDecor, LV_TEXT_DECOR_STRIKETHROUGH);
    expectInt("text-decoration-line: underline line-through", "both", &MW::textDecor,
              LV_TEXT_DECOR_UNDERLINE | LV_TEXT_DECOR_STRIKETHROUGH);
    expectInt("text-decoration: none", "nod", &MW::textDecor, LV_TEXT_DECOR_NONE);
    expectInt("text-decoration: blink: left out", "badd", &MW::textDecor, -1);
    expectInt("on a <p>: its text", "pu", &MW::textDecor, LV_TEXT_DECOR_UNDERLINE);

    printf("\nletter-spacing:\n");
    expectInt("letter-spacing: 2px", "ls", &MW::letterSpace, 2);
    expectInt("letter-spacing: 0.25em", "lsem", &MW::letterSpace, 4);
    expectInt("on a <div>: LVGL passes it to the text inside", "lsdiv", &MW::letterSpace, 3);

    printf("\nline-height (the mock's 16px font is 16px high):\n");
    expectInt("line-height: 1.5: 24px, 8 between lines", "lh", &MW::lineSpace, 8);
    expectInt("line-height: 30px", "lhpx", &MW::lineSpace, 14);
    expectInt("line-height: 200%", "lhpct", &MW::lineSpace, 16);
    expectInt("line-height: 1.25 with font-size: 32px", "lhbig", &MW::lineSpace, 8);

    printf("\nOne line:\n");
    {
        const MW* w = byId("dots");
        expectTrue("nowrap, ellipsis: one line with dots", w && w->longMode == LV_LABEL_LONG_DOT && w->h == 16 && w->w == 100,
                   w ? "mode " + std::to_string(w->longMode) + " " + std::to_string(w->w) + "x" + std::to_string(w->h) : "");
        w = byId("cut");
        expectTrue("nowrap with a width: one line, cut", w && w->longMode == LV_LABEL_LONG_CLIP && w->h == 16,
                   w ? "mode " + std::to_string(w->longMode) + " h " + std::to_string(w->h) : "");
        w = byId("nw");
        expectTrue("nowrap: as wide as its text", w && w->w == LV_SIZE_CONTENT, w ? "w " + std::to_string(w->w) : "");
        w = byId("onlydots");
        expectTrue("ellipsis without nowrap: the text wraps, as in CSS", w && w->longMode == -1,
                   w ? "mode " + std::to_string(w->longMode) : "");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS TEXT TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS TEXT TESTS PASSED ===\n", passed, total);
    return 1;
}
