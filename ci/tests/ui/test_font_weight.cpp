/**
 * Test: the bold face — font-weight, the weight in font: bold 32px, <h1>…<h6> and <th> bold as
 * in a browser. In the mock the bold faces are stand-ins (UI::Font, no files): the test sees
 * which face the engine asks for. See docs/CSS.md, docs/HTML_TAGS.md.
 */
#include <cstdio>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
#include "utils/font.h"

using MW = LvglMock::Widget;

static const char* APP = R"HTML(
<app>
  <style>
    .w700 { font-weight: 700; }
    .w600 { font-weight: 600; }
    .w550 { font-weight: 550; }
    .wbold { font-weight: bold; }
    .wbolder { font-weight: bolder; }
    .wlighter { font-weight: lighter; }
    .wbad { font-weight: heavy; }
    .short { font: bold 32px sans-serif; }
    .shortsize { font: 32px sans-serif; }
    .size32 { font-size: 32px; }
    .plain { font-weight: normal; }
    th.plain { font-weight: 400; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="w700" id="w700">700</label>
      <label class="w600" id="w600">600</label>
      <label class="w550" id="w550">550</label>
      <label class="wbold" id="wbold">bold</label>
      <label class="wbolder" id="wbolder">bolder</label>
      <label class="wlighter" id="wlighter">lighter</label>
      <label class="wbad" id="wbad">heavy</label>
      <label id="inline" style="font-weight: bold">style=""</label>
      <label class="short" id="short">font: bold 32px</label>
      <label class="shortsize" id="shortsize">font: 32px</label>
      <h1 id="h1">Heading 1</h1>
      <h2 id="h2">Heading 2</h2>
      <h4 id="h4">Heading 4</h4>
      <h2 class="plain" id="h2plain">Heading, normal</h2>
      <h3 class="size32" id="h3big">Heading 3 at 32px</h3>
      <p id="p">Paragraph</p>
      <p class="wbold" id="pbold">Bold paragraph</p>
      <table>
        <tr><th id="th">Header</th><th class="plain" id="thplain">Plain header</th></tr>
        <tr><td id="td">Cell</td><td>Cell</td></tr>
      </table>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

static std::string faceOf(const MW* w) {
    if (!w) return "no widget";
    const auto* f = static_cast<const lv_font_t*>(w->font);
    if (!f) return "no font of its own";
    return std::to_string(UI::Font::sizeOf(f)) + (UI::Font::isBold(f) ? " bold" : " regular");
}

// The face the element has: "32 bold", "16 regular"; "-" for none of its own
static void expectFace(const char* name, const char* id, const std::string& want) {
    const MW* w = byId(id);
    const std::string got = (w && !w->font) ? "-" : faceOf(w);
    TEST(name) {
        if (got == want) PASS();
        else FAIL(("got " + got).c_str());
    }
}

int main() {
    printf("=== FONT WEIGHT ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("UI::Font:\n");
    TEST("get(32, true): the bold face of 32px") {
        const lv_font_t* f = UI::Font::get(32, true);
        if (f != UI::Font::get(32) && UI::Font::isBold(f) && UI::Font::sizeOf(f) == 32) PASS();
        else FAIL("");
    }
    TEST("get(22, true): the nearest size, as get(22)") {
        if (UI::Font::get(22, true) == UI::Font::get(24, true) && UI::Font::sizeOf(UI::Font::get(22, true)) == 24) PASS();
        else FAIL("");
    }
    TEST("the regular faces are not bold") {
        if (!UI::Font::isBold(UI::Font::get(16)) && !UI::Font::isBold(nullptr)) PASS();
        else FAIL("");
    }

    printf("\nfont-weight:\n");
    expectFace("font-weight: 700", "w700", "16 bold");
    expectFace("font-weight: 600: bold, the nearer face", "w600", "16 bold");
    expectFace("font-weight: 550: regular, the nearer face", "w550", "16 regular");
    expectFace("font-weight: bold", "wbold", "16 bold");
    expectFace("font-weight: bolder: bold over the regular", "wbolder", "16 bold");
    expectFace("font-weight: lighter: regular", "wlighter", "16 regular");
    expectFace("font-weight: heavy: left out, the label's own", "wbad", "16 regular");
    expectFace("style=\"font-weight: bold\"", "inline", "16 bold");
    expectFace("font: bold 32px sans-serif", "short", "32 bold");
    expectFace("font: 32px sans-serif: the size, no weight", "shortsize", "32 regular");

    printf("\nHTML: headings and table headers are bold, as in a browser:\n");
    expectFace("<h1>: 32, bold", "h1", "32 bold");
    expectFace("<h2>: 24, bold", "h2", "24 bold");
    expectFace("<h4>: 16, bold", "h4", "16 bold");
    expectFace("<h2> with font-weight: normal", "h2plain", "24 regular");
    expectFace("<h3> with font-size: 32px: still bold", "h3big", "32 bold");
    expectFace("<p>: the font of the text around", "p", "-");
    expectFace("<p> with font-weight: bold", "pbold", "16 bold");
    expectFace("<th>: bold", "th", "16 bold");
    expectFace("<th> with font-weight: 400: as the page says", "thplain", "16 regular");

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d FONT WEIGHT TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d FONT WEIGHT TESTS PASSED ===\n", passed, total);
    return 1;
}
