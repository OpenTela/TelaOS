/**
 * Test: the italic face — font-style, the style in font: italic 20px, <i>, <em>, <cite>, <var>,
 * <dfn> and <address> italic as in a browser, bold italic where both meet. In the mock the faces
 * kept as files are stand-ins (UI::Font, no files): the test sees which face the engine asks for.
 * See docs/CSS.md, docs/HTML_TAGS.md.
 */
#include <cstdio>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
#include "ui/css_values.h"
#include "utils/font.h"

using MW = LvglMock::Widget;

static const char* APP = R"HTML(
<app>
  <style>
    .italic { font-style: italic; }
    .oblique { font-style: oblique; }
    .angle { font-style: oblique 10deg; }
    .upright { font-style: normal; }
    .bad { font-style: slanted; }
    .short { font: italic bold 20px sans-serif; }
    address.upright { font-style: normal; }
    .s28 { font-size: 28px; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="italic" id="italic">italic</label>
      <label class="oblique" id="oblique">oblique</label>
      <label class="angle" id="angle">oblique 10deg</label>
      <label class="upright" id="upright">normal</label>
      <label class="bad" id="bad">slanted</label>
      <label id="inline" style="font-style: italic">style=""</label>
      <label class="short" id="short">font: italic bold 20px</label>
      <h2 class="italic" id="h2">Heading</h2>
      <p class="italic" id="p">Paragraph</p>
      <address id="addr">watch@example.com</address>
      <address class="upright" id="addrUp">upright@example.com</address>
      <address class="s28" id="addr28">big@example.com</address>
      <p><i>One</i> <em>Two</em> <cite>Three</cite> <var>Four</var> <dfn>Five</dfn> <b><i>Six</i></b>
         <span style="font-style: italic">Seven</span> <i style="font-style: normal">Eight</i>
         <i style="font-size: 20px">Nine</i><sup>1</sup></p>
    </page>
  </ui>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }

// A word of text drawn word by word (a line with <sup>): its label, the space after it left out
static const MW* word(const MW* w, const std::string& text) {
    if (!w) return nullptr;
    for (const MW* c : w->children) {
        std::string t = c->text;
        while (!t.empty() && t.back() == ' ') t.pop_back();
        if (t == text) return c;
        if (const MW* found = word(c, text)) return found;
    }
    return nullptr;
}

// "20 bold italic", "16 italic", "16": the face of the font the widget has of its own; "-" for none
static std::string faceOf(const MW* w) {
    if (!w) return "no widget";
    const auto* f = static_cast<const lv_font_t*>(w->font);
    if (!f) return "-";
    return std::to_string(UI::Font::sizeOf(f)) + (UI::Font::isBold(f) ? " bold" : "") +
           (UI::Font::isItalic(f) ? " italic" : "");
}

static void expectFace(const char* name, const MW* w, const std::string& want) {
    const std::string got = faceOf(w);
    TEST(name) {
        if (got == want) PASS();
        else FAIL(("got " + got).c_str());
    }
}

int main() {
    printf("=== FONT STYLE ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("UI::Font:\n");
    TEST("get(20, false, true): the italic face of 20px") {
        const lv_font_t* f = UI::Font::get(20, false, true);
        if (f != UI::Font::get(20) && UI::Font::isItalic(f) && !UI::Font::isBold(f) && UI::Font::sizeOf(f) == 20) PASS();
        else FAIL("");
    }
    TEST("get(32, true, true): bold italic, apart from both") {
        const lv_font_t* f = UI::Font::get(32, true, true);
        if (UI::Font::isItalic(f) && UI::Font::isBold(f) && f != UI::Font::get(32, true) &&
            f != UI::Font::get(32, false, true) && UI::Font::sizeOf(f) == 32)
            PASS();
        else FAIL("");
    }
    TEST("the fonts in flash and bold are not italic") {
        if (!UI::Font::isItalic(UI::Font::get(16)) && !UI::Font::isItalic(UI::Font::get(48, true)) &&
            !UI::Font::isItalic(nullptr))
            PASS();
        else FAIL("");
    }
    TEST("cssFontStyle: normal, italic, oblique, oblique 10deg") {
        if (UI::cssFontStyle("normal") == UI::kCssNormal && UI::cssFontStyle(" italic ") == UI::kCssItalic &&
            UI::cssFontStyle("oblique") == UI::kCssItalic && UI::cssFontStyle("oblique -10deg") == UI::kCssItalic &&
            UI::cssFontStyle("slanted") == 0 && UI::cssFontStyle("oblique 10") == 0 && UI::cssFontStyle("") == 0)
            PASS();
        else FAIL("");
    }

    printf("\nfont-style:\n");
    expectFace("font-style: italic", byId("italic"), "16 italic");
    expectFace("font-style: oblique: italic, the face there is", byId("oblique"), "16 italic");
    expectFace("font-style: oblique 10deg", byId("angle"), "16 italic");
    expectFace("font-style: normal", byId("upright"), "16");
    expectFace("font-style: slanted: left out", byId("bad"), "16");
    expectFace("style=\"font-style: italic\"", byId("inline"), "16 italic");
    expectFace("font: italic bold 20px sans-serif", byId("short"), "20 bold italic");
    expectFace("<h2> with font-style: italic: bold italic", byId("h2"), "24 bold italic");
    expectFace("<p> with font-style: italic", byId("p"), "16 italic");

    printf("\nHTML: italic as in a browser:\n");
    expectFace("<address>", byId("addr"), "16 italic");
    expectFace("<address> with font-style: normal", byId("addrUp"), "16");
    expectFace("<address> with font-size: 28px: still italic", byId("addr28"), "28 italic");
    const MW* screen = LvglMock::g_screen;
    expectFace("<i>", word(screen, "One"), "16 italic");
    expectFace("<em>", word(screen, "Two"), "16 italic");
    expectFace("<cite>", word(screen, "Three"), "16 italic");
    expectFace("<var>", word(screen, "Four"), "16 italic");
    expectFace("<dfn>", word(screen, "Five"), "16 italic");
    expectFace("<b><i>: bold italic", word(screen, "Six"), "16 bold italic");
    expectFace("<span style=\"font-style: italic\">", word(screen, "Seven"), "16 italic");
    expectFace("<i style=\"font-style: normal\">: upright", word(screen, "Eight"), "16");
    expectFace("<i style=\"font-size: 20px\">: 20, italic", word(screen, "Nine"), "20 italic");

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d FONT STYLE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d FONT STYLE TESTS PASSED ===\n", passed, total);
    return 1;
}
