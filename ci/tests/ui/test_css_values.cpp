/**
 * Test: CSS values — colors with alpha (rgb(), hsl(), #RRGGBBAA, transparent), lengths (em, rem,
 * vw, vh), 1 to 4 values in padding and gap, custom properties and var(), and values that do not
 * read (their declarations are left out). See docs/CSS.md.
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
    :root { --accent: #123456; --p: 2px; --Case: #0C0C0C; }
    .rgb { color: rgb(255, 0, 0); }
    .rgbsp { color: rgb(0 128 255); }
    .rgba { color: rgba(0, 0, 255, 0.5); }
    .hsl { color: hsl(120, 100%, 50%); }
    .hslsp { color: hsl(240deg 100% 50% / 25%); }
    .turn { color: hsl(0.5turn 100% 50%); }
    .hex8 { color: #11223344; }
    .hex4 { color: #1234; }
    .bga { background: rgba(0, 0, 0, 0.5); }
    .clear { background: transparent; }
    .url { background: url(x.png) #222222 no-repeat; }
    .badc { color: rgb(1, 2); }
    .badn { color: notacolor; }
    .p2 { padding: 4px 8px; }
    .p3 { padding: 1px 2px 3px; }
    .p4 { padding: 1px 2px 3px 4px; }
    .pbare { padding: 4 8; }
    .pbad { padding: 4px foo; }
    .pem { font-size: 32px; padding: 1em; }
    .prem { padding: 1rem; }
    .vw { width: 50vw; }
    .vh { height: 10vh; }
    .wem { width: 2em; }
    .fem { font-size: 2em; }
    .fpct { font-size: 300%; }
    .fbad { font-size: inherit; }
    .gap { gap: 4px 8px; }
    .rgap { row-gap: 5px; column-gap: 7px; }
    .circle { border-radius: 50%; }
    .r2 { border-radius: 6px 2px; }
    .op { opacity: 50%; }
    .v1 { color: var(--accent); }
    .v2 { color: var(--missing, #654321); }
    .v3 { color: var(--missing, var(--accent)); }
    .vbox { --c: #0A0B0C; }
    .vin { color: var(--c); }
    .vown { --c: #0D0E0F; color: var(--c); }
    .v5 { padding: var(--p) 8px; }
    .v6 { color: var(--nothing); }
    .vcase { color: var(--case, #FEFEFE); }
    .vcycle { --a: var(--b); --b: var(--a); color: var(--a, #0F0F0F); }
    .semi { color: rgba(255, 0, 0, 0.5); }
    .big2 { font-size: 2em; }
  </style>
  <ui default="/main">
    <page id="main">
      <label class="rgb" id="rgb">rgb</label>
      <label class="rgbsp" id="rgbsp">rgb spaces</label>
      <label class="rgba" id="rgba">rgba</label>
      <label class="hsl" id="hsl">hsl</label>
      <label class="hslsp" id="hslsp">hsl spaces</label>
      <label class="turn" id="turn">turn</label>
      <label class="hex8" id="hex8">hex8</label>
      <label class="hex4" id="hex4">hex4</label>
      <label class="bga" id="bga">bg alpha</label>
      <label class="clear" id="clear">transparent</label>
      <label class="url" id="url">url</label>
      <label class="badc" id="badc">bad rgb</label>
      <label class="badn" id="badn">bad name</label>
      <label class="p2" id="p2">p2</label>
      <label class="p3" id="p3">p3</label>
      <label class="p4" id="p4">p4</label>
      <label class="pbare" id="pbare">pbare</label>
      <label class="pbad" id="pbad">pbad</label>
      <label class="pem" id="pem">pem</label>
      <label class="prem" id="prem">prem</label>
      <label class="vw" id="vw">vw</label>
      <label class="vh" id="vh">vh</label>
      <label class="wem" id="wem">wem</label>
      <label class="fem" id="fem">fem</label>
      <label class="fpct" id="fpct">fpct</label>
      <label class="fbad" id="fbad" font="32">fbad</label>
      <div class="gap" id="gap" flex="row"><label>a</label></div>
      <div class="rgap" id="rgap" flex="row"><label>a</label></div>
      <label class="circle" id="circle">circle</label>
      <label class="r2" id="r2">r2</label>
      <label class="op" id="op">op</label>
      <label class="v1" id="v1">v1</label>
      <label class="v2" id="v2">v2</label>
      <label class="v3" id="v3">v3</label>
      <div class="vbox"><label class="vin" id="vin">inherited</label><label class="vown" id="vown">own</label></div>
      <label class="v5" id="v5">v5</label>
      <label class="v6" id="v6">v6</label>
      <label class="vcase" id="vcase">vcase</label>
      <label class="vcycle" id="vcycle">vcycle</label>
      <p id="vinl" style="--c: #102030; color: var(--c)">inline</p>
      <p id="runs">x <a href="/a">a</a> <a href="/b">b</a> <span class="semi">s</span> <span class="big2">B</span></p>
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

static std::string hex(uint32_t c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%06X", c);
    return buf;
}

static void expectColor(const char* name, const MW* w, uint32_t want, int opa = 255) {
    TEST(name) {
        if (w && w->hasColor && w->color == want && w->textOpa == opa) PASS();
        else if (!w) FAIL("no widget");
        else FAIL(("color " + (w->hasColor ? hex(w->color) : std::string("not set")) + " opa " + std::to_string(w->textOpa)).c_str());
    }
}

static void expectNoColor(const char* name, const MW* w) {
    TEST(name) {
        if (w && !w->hasColor) PASS();
        else FAIL(w ? ("color " + hex(w->color)).c_str() : "no widget");
    }
}

static void expectTrue(const char* name, bool ok, const std::string& why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why.c_str());
    }
}

static std::string pads(const MW* w) {
    if (!w) return "no widget";
    return "all " + std::to_string(w->padAll) + ", t r b l " + std::to_string(w->padTop) + " " + std::to_string(w->padRight) +
           " " + std::to_string(w->padBottom) + " " + std::to_string(w->padLeft);
}

static void expectPads(const char* name, const MW* w, int t, int r, int b, int l) {
    expectTrue(name, w && w->padAll == -1 && w->padTop == t && w->padRight == r && w->padBottom == b && w->padLeft == l,
               pads(w));
}

int main() {
    printf("=== CSS VALUES ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.render(APP);

    printf("Colors:\n");
    expectColor("rgb(255, 0, 0)", byId("rgb"), 0xFF0000);
    expectColor("rgb(0 128 255): spaces", byId("rgbsp"), 0x0080FF);
    expectColor("rgba(0, 0, 255, 0.5): alpha is text_opa", byId("rgba"), 0x0000FF, 128);
    expectColor("hsl(120, 100%, 50%)", byId("hsl"), 0x00FF00);
    expectColor("hsl(240deg 100% 50% / 25%)", byId("hslsp"), 0x0000FF, 64);
    expectColor("hsl(0.5turn 100% 50%)", byId("turn"), 0x00FFFF);
    expectColor("#11223344", byId("hex8"), 0x112233, 0x44);
    expectColor("#1234", byId("hex4"), 0x112233, 0x44);
    {
        const MW* w = byId("bga");
        expectTrue("background: rgba(0, 0, 0, 0.5): bg_opa 128", w && w->hasBgcolor && w->bgcolor == 0 && w->bgOpa == 128,
                   w ? "bg " + hex(w->bgcolor) + " opa " + std::to_string(w->bgOpa) : "no widget");
        w = byId("clear");
        expectTrue("background: transparent: no background (was black)", w && w->bgOpa == 0,
                   w ? "opa " + std::to_string(w->bgOpa) : "no widget");
        w = byId("url");
        expectTrue("background: url(x.png) #222222 no-repeat: the color", w && w->bgcolor == 0x222222 && w->bgOpa == 255,
                   w ? "bg " + hex(w->bgcolor) : "no widget");
    }
    expectNoColor("rgb(1, 2): left out (was black)", byId("badc"));
    expectNoColor("notacolor: left out (was black)", byId("badn"));

    printf("\nLengths:\n");
    expectPads("padding: 4px 8px", byId("p2"), 4, 8, 4, 8);
    expectPads("padding: 1px 2px 3px", byId("p3"), 1, 2, 3, 2);
    expectPads("padding: 1px 2px 3px 4px", byId("p4"), 1, 2, 3, 4);
    expectPads("padding: 4 8 (was 48)", byId("pbare"), 4, 8, 4, 8);
    expectPads("padding: 4px foo: left out", byId("pbad"), 0, 0, 0, 0);
    expectTrue("padding: 1em with font-size: 32px", byId("pem") && byId("pem")->padAll == 32, pads(byId("pem")));
    expectTrue("padding: 1rem", byId("prem") && byId("prem")->padAll == 16, pads(byId("prem")));
    expectTrue("width: 50vw", byId("vw") && byId("vw")->w == 240, byId("vw") ? std::to_string(byId("vw")->w) : "");
    expectTrue("height: 10vh", byId("vh") && byId("vh")->h == 48, byId("vh") ? std::to_string(byId("vh")->h) : "");
    expectTrue("width: 2em (16px text)", byId("wem") && byId("wem")->w == 32, byId("wem") ? std::to_string(byId("wem")->w) : "");
    expectTrue("font-size: 2em", byId("fem") && byId("fem")->fontSize == 32,
               byId("fem") ? std::to_string(byId("fem")->fontSize) : "");
    expectTrue("font-size: 300%", byId("fpct") && byId("fpct")->fontSize == 48,
               byId("fpct") ? std::to_string(byId("fpct")->fontSize) : "");
    expectTrue("font-size: inherit: left out, font=\"32\" stays (was 16px)", byId("fbad") && byId("fbad")->fontSize == 32,
               byId("fbad") ? std::to_string(byId("fbad")->fontSize) : "");
    expectTrue("gap: 4px 8px: rows, then columns", byId("gap") && byId("gap")->padRow == 4 && byId("gap")->padColumn == 8,
               byId("gap") ? std::to_string(byId("gap")->padRow) + " " + std::to_string(byId("gap")->padColumn) : "");
    expectTrue("row-gap, column-gap", byId("rgap") && byId("rgap")->padRow == 5 && byId("rgap")->padColumn == 7,
               byId("rgap") ? std::to_string(byId("rgap")->padRow) + " " + std::to_string(byId("rgap")->padColumn) : "");
    expectTrue("border-radius: 50%: a circle", byId("circle") && byId("circle")->radius == LV_RADIUS_CIRCLE,
               byId("circle") ? std::to_string(byId("circle")->radius) : "");
    expectTrue("border-radius: 6px 2px: the first", byId("r2") && byId("r2")->radius == 6,
               byId("r2") ? std::to_string(byId("r2")->radius) : "");
    expectTrue("opacity: 50%", byId("op") && byId("op")->opacity == 128, byId("op") ? std::to_string(byId("op")->opacity) : "");

    printf("\nvar():\n");
    expectColor(":root { --accent } and var(--accent)", byId("v1"), 0x123456);
    expectColor("var(--missing, #654321): the fallback", byId("v2"), 0x654321);
    expectColor("var(--missing, var(--accent))", byId("v3"), 0x123456);
    expectColor("--c on a <div> is inherited", byId("vin"), 0x0A0B0C);
    expectColor("the element's own --c wins", byId("vown"), 0x0D0E0F);
    expectPads("padding: var(--p) 8px", byId("v5"), 2, 8, 2, 8);
    expectNoColor("var(--nothing) without a fallback: left out", byId("v6"));
    expectColor("--Case and --case are two properties", byId("vcase"), 0xFEFEFE);
    expectColor("--a and --b in a cycle: the fallback", byId("vcycle"), 0x0F0F0F);
    expectColor("style=\"--c: ...; color: var(--c)\"", byId("vinl"), 0x102030);

    printf("\nText runs:\n");
    expectColor("<span> color: rgba(255, 0, 0, 0.5)", word(byId("runs"), "s"), 0xFF0000, 128);
    {
        const MW* b = word(byId("runs"), "B");
        expectTrue("<span> font-size: 2em", b && b->fontSize == 32, b ? std::to_string(b->fontSize) : "no widget");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CSS VALUE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CSS VALUE TESTS PASSED ===\n", passed, total);
    return 1;
}
