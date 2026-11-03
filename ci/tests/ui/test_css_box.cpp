/**
 * Test: the CSS box — margin, border, outline, box-shadow, linear-gradient() backgrounds and
 * overflow, as LVGL draws them (one border, one outline, one outer shadow, two-color gradients
 * along an axis). See docs/CSS.md.
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
    .m2 { margin: 4px 8px; }
    .mauto { margin: 0 auto; }
    .mneg { margin-top: -6px; }
    .mem { margin: 1em; }
    .b1 { border: 2px solid #FF0000; }
    .bnostyle { border: 1px #FF0000; }
    .bbottom { border-bottom: 3px dashed rgba(0, 0, 255, 0.5); }
    .blast { border: 1px solid #111111; border-left: 4px solid #222222; }
    .bparts { border-width: 0 0 2px; border-style: solid; border-color: #333333; }
    .nob { border: none; }
    .cur { color: #445566; border: 2px solid; }
    .thick { border: thick double #777777; }
    .o1 { outline: 2px solid #00FF00; outline-offset: 3px; }
    .onone { outline: none; }
    .s1 { box-shadow: 2px 4px 8px 1px rgba(0, 0, 0, 0.5); }
    .s2 { box-shadow: inset 0 0 4px #000000, 3px 3px #123456; }
    .snone { box-shadow: none; }
    .sbad { box-shadow: 2px; }
    .g1 { background: linear-gradient(#FF0000, #0000FF); }
    .g2 { background: linear-gradient(to right, #FF0000 20%, #00FF00, #0000FF 80%); }
    .g3 { background-image: linear-gradient(to top, #111111, #222222); }
    .g4 { background: linear-gradient(45deg, red, blue); }
    .g5 { background: linear-gradient(90deg, rgba(255, 0, 0, 0.5), #00F); }
    .ohidden { overflow: hidden; }
    .oauto { overflow: auto; height: 40px; }
    .oy { overflow-y: scroll; height: 40px; }
    .ovisible { overflow: visible; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="m2" id="m2">m2</label>
      <label class="mauto" id="mauto">mauto</label>
      <label class="mneg" id="mneg">mneg</label>
      <label class="mem" id="mem">mem</label>
      <label class="b1" id="b1">b1</label>
      <label class="bnostyle" id="bnostyle">bnostyle</label>
      <label class="bbottom" id="bbottom">bbottom</label>
      <label class="blast" id="blast">blast</label>
      <label class="bparts" id="bparts">bparts</label>
      <fieldset class="nob" id="nob"><legend>L</legend>x</fieldset>
      <label class="cur" id="cur">cur</label>
      <label class="thick" id="thick">thick</label>
      <label class="o1" id="o1">o1</label>
      <label class="onone" id="onone">onone</label>
      <label class="s1" id="s1">s1</label>
      <label class="s2" id="s2">s2</label>
      <label class="snone" id="snone">snone</label>
      <label class="sbad" id="sbad">sbad</label>
      <div class="g1" id="g1"><label>g1</label></div>
      <div class="g2" id="g2"><label>g2</label></div>
      <div class="g3" id="g3"><label>g3</label></div>
      <div class="g4" id="g4"><label>g4</label></div>
      <div class="g5" id="g5"><label>g5</label></div>
      <div class="ohidden" id="ohidden"><label>a</label></div>
      <div class="oauto" id="oauto"><label>a</label></div>
      <div class="oy" id="oy"><label>a</label></div>
      <div class="ovisible" id="ovisible"><label>a</label></div>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

static std::string hex(uint32_t c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%06X", c);
    return buf;
}

static void expectTrue(const char* name, bool ok, const std::string& why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why.c_str());
    }
}

static std::string margins(const MW* w) {
    if (!w) return "no widget";
    return std::to_string(w->marginTop) + " " + std::to_string(w->marginRight) + " " + std::to_string(w->marginBottom) +
           " " + std::to_string(w->marginLeft);
}

static void expectMargins(const char* name, const char* id, int t, int r, int b, int l) {
    const MW* w = byId(id);
    expectTrue(name, w && w->marginTop == t && w->marginRight == r && w->marginBottom == b && w->marginLeft == l,
               margins(w));
}

static std::string border(const MW* w) {
    if (!w) return "no widget";
    return "width " + std::to_string(w->borderWidth) + " color " + hex(w->borderColor) + " opa " +
           std::to_string(w->borderOpa) + " side " + std::to_string(w->borderSide);
}

static void expectBorder(const char* name, const char* id, int width, uint32_t color, int opa, int side) {
    const MW* w = byId(id);
    expectTrue(name, w && w->borderWidth == width && w->borderColor == color && w->borderOpa == opa && w->borderSide == side,
               border(w));
}

static std::string shadow(const MW* w) {
    if (!w) return "no widget";
    return "width " + std::to_string(w->shadowWidth) + " at " + std::to_string(w->shadowX) + "," +
           std::to_string(w->shadowY) + " spread " + std::to_string(w->shadowSpread) + " " + hex(w->shadowColor) +
           " opa " + std::to_string(w->shadowOpa);
}

static std::string gradient(const MW* w) {
    if (!w) return "no widget";
    return "dir " + std::to_string(w->gradDir) + " " + hex(w->bgcolor) + " " + std::to_string(w->mainStop) + " → " +
           hex(w->gradColor) + " " + std::to_string(w->gradStop) + ", opa " + std::to_string(w->mainOpa) + "/" +
           std::to_string(w->gradOpa);
}

static void expectGradient(const char* name, const char* id, int dir, uint32_t from, int fromStop, uint32_t to,
                           int toStop) {
    const MW* w = byId(id);
    expectTrue(name, w && w->gradDir == dir && w->bgcolor == from && w->mainStop == fromStop && w->gradColor == to &&
                         w->gradStop == toStop,
               gradient(w));
}

static std::string flow(const MW* w) {
    if (!w) return "no widget";
    return std::string("scrollable ") + (w->scrollable ? "yes" : "no") + ", overflow visible " +
           (w->overflowVisible ? "yes" : "no") + ", dir " + std::to_string(w->scrollDir) + ", bar " +
           std::to_string(w->scrollbarMode);
}

int main() {
    printf("=== CSS BOX ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("margin:\n");
    expectMargins("margin: 4px 8px", "m2", 4, 8, 4, 8);
    expectMargins("margin: 0 auto: auto is 0, no centering", "mauto", 0, 0, 0, 0);
    expectMargins("margin-top: -6px", "mneg", -6, 0, 0, 0);
    expectMargins("margin: 1em", "mem", 16, 16, 16, 16);

    printf("\nborder:\n");
    expectBorder("border: 2px solid #FF0000", "b1", 2, 0xFF0000, 255, LV_BORDER_SIDE_FULL);
    {
        const MW* w = byId("bnostyle");
        expectTrue("border: 1px #FF0000: no style, no border (as in a browser)", w && w->borderWidth == 0, border(w));
    }
    expectBorder("border-bottom: 3px dashed rgba(0, 0, 255, 0.5)", "bbottom", 3, 0x0000FF, 128, LV_BORDER_SIDE_BOTTOM);
    expectBorder("border, then border-left: the side set last", "blast", 4, 0x222222, 255, LV_BORDER_SIDE_FULL);
    expectBorder("border-width: 0 0 2px and style, color: the bottom", "bparts", 2, 0x333333, 255, LV_BORDER_SIDE_BOTTOM);
    {
        const MW* w = byId("nob");
        expectTrue("border: none takes the <fieldset> frame away", w && w->borderWidth == 0, border(w));
    }
    expectBorder("border: 2px solid: the text's color", "cur", 2, 0x445566, 255, LV_BORDER_SIDE_FULL);
    expectBorder("border: thick double: 5px, drawn solid", "thick", 5, 0x777777, 255, LV_BORDER_SIDE_FULL);

    printf("\noutline:\n");
    {
        const MW* w = byId("o1");
        expectTrue("outline: 2px solid #00FF00; outline-offset: 3px",
                   w && w->outlineWidth == 2 && w->outlineColor == 0x00FF00 && w->outlinePad == 3,
                   w ? std::to_string(w->outlineWidth) + " " + hex(w->outlineColor) + " pad " + std::to_string(w->outlinePad)
                     : "no widget");
        w = byId("onone");
        expectTrue("outline: none", w && w->outlineWidth == 0, w ? std::to_string(w->outlineWidth) : "no widget");
    }

    printf("\nbox-shadow:\n");
    {
        const MW* w = byId("s1");
        expectTrue("box-shadow: 2px 4px 8px 1px rgba(0, 0, 0, 0.5)",
                   w && w->shadowWidth == 8 && w->shadowX == 2 && w->shadowY == 4 && w->shadowSpread == 1 &&
                       w->shadowColor == 0 && w->shadowOpa == 128,
                   shadow(w));
        w = byId("s2");
        expectTrue("inset, then 3px 3px #123456: the outer one, blur 0 → 1",
                   w && w->shadowWidth == 1 && w->shadowX == 3 && w->shadowY == 3 && w->shadowColor == 0x123456, shadow(w));
        w = byId("snone");
        expectTrue("box-shadow: none", w && w->shadowWidth == 0, shadow(w));
        w = byId("sbad");
        expectTrue("box-shadow: 2px: left out", w && w->shadowWidth == 0 && w->shadowX == 0, shadow(w));
    }

    printf("\nlinear-gradient():\n");
    expectGradient("linear-gradient(#FF0000, #0000FF): down", "g1", LV_GRAD_DIR_VER, 0xFF0000, 0, 0x0000FF, 255);
    expectGradient("to right, #FF0000 20%, #00FF00, #0000FF 80%", "g2", LV_GRAD_DIR_HOR, 0xFF0000, 51, 0x0000FF, 204);
    expectGradient("background-image: to top: the other way round", "g3", LV_GRAD_DIR_VER, 0x222222, 0, 0x111111, 255);
    {
        const MW* w = byId("g4");
        expectTrue("45deg: left out (LVGL draws along an axis)", w && w->gradDir == LV_GRAD_DIR_NONE, gradient(w));
        w = byId("g5");
        expectTrue("90deg, rgba(255, 0, 0, 0.5): alpha of the stop",
                   w && w->gradDir == LV_GRAD_DIR_HOR && w->mainOpa == 128 && w->gradOpa == 255, gradient(w));
    }

    printf("\noverflow:\n");
    {
        const MW* w = byId("ohidden");
        expectTrue("overflow: hidden: no scrolling, clipped", w && !w->scrollable && !w->overflowVisible, flow(w));
        w = byId("oauto");
        expectTrue("overflow: auto: scrolls both ways, bar when needed",
                   w && w->scrollable && w->scrollDir == LV_DIR_ALL && w->scrollbarMode == LV_SCROLLBAR_MODE_AUTO, flow(w));
        w = byId("oy");
        expectTrue("overflow-y: scroll: down only, bar always",
                   w && w->scrollable && w->scrollDir == LV_DIR_VER && w->scrollbarMode == LV_SCROLLBAR_MODE_ON, flow(w));
        w = byId("ovisible");
        expectTrue("overflow: visible: content shows outside", w && !w->scrollable && w->overflowVisible, flow(w));
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS BOX TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS BOX TESTS PASSED ===\n", passed, total);
    return 1;
}
