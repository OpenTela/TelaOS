/**
 * Test: the sizes text is drawn at — the nearest of twelve, 12 … 72 (the larger of two as near):
 * font-size, the size in font:, font= of TelaOS widgets, <font size>, <big>. In the mock the sizes
 * kept as files are stand-ins (UI::Font, no files): the test sees which size the engine asks for.
 * See docs/CSS.md, docs/HTML_TAGS.md.
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
    .s20 { font-size: 20px; }
    .s14 { font-size: 14px; }
    .s21 { font-size: 21px; }
    .s22 { font-size: 22px; }
    .em { font-size: 1.25em; }
    .pct { font-size: 150%; }
    .s9 { font-size: 9px; }
    .s100 { font-size: 100px; }
    .s60 { font-size: 60px; }
    .s56 { font-size: 56px; }
    .short { font: bold 28px sans-serif; }
    .s40 { font-size: 40px; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="s20" id="s20">20px</label>
      <label class="s14" id="s14">14px</label>
      <label class="s21" id="s21">21px</label>
      <label class="s22" id="s22">22px</label>
      <label class="em" id="em">1.25em</label>
      <label class="pct" id="pct">150%</label>
      <label class="s9" id="s9">9px</label>
      <label class="s100" id="s100">100px</label>
      <label class="s60" id="s60">60px</label>
      <label class="s56" id="s56">56px</label>
      <label class="short" id="short">font: bold 28px</label>
      <label font="24" id="f24">font=24</label>
      <label font="18" id="f18">font=18</label>
      <label font="32" id="f32">font=32</label>
      <p class="s20" id="p20">Paragraph at 20px</p>
      <h1 class="s40" id="h40">Heading at 40px</h1>
      <p><big>Big</big> text<sup>1</sup></p>
      <p><font size="1">One</font> <font size="2">Two</font> <font size="4">Four</font> <font size="5">Five</font>
         <font size="6">Six</font> <font size="7">Seven</font> <font size="+1">Plus</font>
         <font size="-1">Minus</font><sup>2</sup></p>
      <h2><big>Larger</big> heading<sup>3</sup></h2>
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

static std::string faceOf(const MW* w) {
    if (!w) return "no widget";
    const auto* f = static_cast<const lv_font_t*>(w->font);
    if (!f) return "no font of its own";
    return std::to_string(UI::Font::sizeOf(f)) + (UI::Font::isBold(f) ? " bold" : "");
}

static void expectFace(const char* name, const MW* w, const std::string& want) {
    const std::string got = faceOf(w);
    TEST(name) {
        if (got == want) PASS();
        else FAIL(("got " + got).c_str());
    }
}

static void expectNearest(int requested, int want) {
    const std::string name = "nearest(" + std::to_string(requested) + ") = " + std::to_string(want);
    TEST(name.c_str()) {
        const int got = UI::Font::nearest(requested);
        if (got == want && UI::Font::sizeOf(UI::Font::get(requested)) == want) PASS();
        else FAIL(("got " + std::to_string(got)).c_str());
    }
}

int main() {
    printf("=== FONT SIZES ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("UI::Font: the nearest of 12 … 72, the larger of two as near:\n");
    expectNearest(20, 20);
    expectNearest(21, 20);
    expectNearest(22, 24);
    expectNearest(13, 14);
    expectNearest(44, 48);
    expectNearest(56, 48);
    expectNearest(60, 72);
    expectNearest(9, 12);
    expectNearest(100, 72);
    expectNearest(0, 16);
    TEST("get(16), get(32): the fonts in flash") {
        if (UI::Font::get(16) == &Ubuntu_16px && UI::Font::get(32) == &Ubuntu_32px) PASS();
        else FAIL("");
    }
    TEST("get(20, true): the bold face of 20px") {
        const lv_font_t* f = UI::Font::get(20, true);
        if (f != UI::Font::get(20) && UI::Font::isBold(f) && UI::Font::sizeOf(f) == 20) PASS();
        else FAIL("");
    }

    printf("\nfont-size:\n");
    expectFace("font-size: 20px", byId("s20"), "20");
    expectFace("font-size: 14px", byId("s14"), "14");
    expectFace("font-size: 21px: 20, the nearer", byId("s21"), "20");
    expectFace("font-size: 22px: 24, the larger of two as near", byId("s22"), "24");
    expectFace("font-size: 1.25em: 20", byId("em"), "20");
    expectFace("font-size: 150%: 24", byId("pct"), "24");
    expectFace("font-size: 9px: 12, the smallest", byId("s9"), "12");
    expectFace("font-size: 100px: 72, the largest", byId("s100"), "72");
    expectFace("font-size: 60px: 72", byId("s60"), "72");
    expectFace("font-size: 56px: 48", byId("s56"), "48");
    expectFace("font: bold 28px sans-serif", byId("short"), "28 bold");
    expectFace("<p> with font-size: 20px", byId("p20"), "20");
    expectFace("<h1> with font-size: 40px: bold", byId("h40"), "40 bold");

    printf("\nfont= of TelaOS widgets:\n");
    expectFace("<label font=\"24\">", byId("f24"), "24");
    expectFace("<label font=\"18\">", byId("f18"), "18");
    expectFace("<label font=\"32\">", byId("f32"), "32");

    printf("\nHTML: the sizes a browser gives:\n");
    const MW* screen = LvglMock::g_screen;
    expectFace("<big>: larger, 16 × 1.2 → 20", word(screen, "Big"), "20");
    expectFace("<font size=\"1\">: 10px → 12", word(screen, "One"), "12");
    expectFace("<font size=\"2\">: 13px → 14", word(screen, "Two"), "14");
    expectFace("<font size=\"4\">: 18px", word(screen, "Four"), "18");
    expectFace("<font size=\"5\">: 24px", word(screen, "Five"), "24");
    expectFace("<font size=\"6\">: 32px", word(screen, "Six"), "32");
    expectFace("<font size=\"7\">: 48px", word(screen, "Seven"), "48");
    expectFace("<font size=\"+1\">: 4, 18px", word(screen, "Plus"), "18");
    expectFace("<font size=\"-1\">: 2, 13px → 14", word(screen, "Minus"), "14");
    // the size only: a word of the mock has no font it inherits, so it is not bold here
    const MW* larger = word(screen, "Larger");
    TEST("<big> in <h2>: 24 × 1.2 → 29, drawn at 28") {
        if (larger && larger->font && UI::Font::sizeOf(static_cast<const lv_font_t*>(larger->font)) == 28) PASS();
        else FAIL(("got " + faceOf(larger)).c_str());
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d FONT SIZE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d FONT SIZE TESTS PASSED ===\n", passed, total);
    return 1;
}
