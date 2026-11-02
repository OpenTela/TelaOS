/**
 * test_html_tags.cpp — standard HTML elements (MDN element reference), see docs/HTML_TAGS.md
 *
 * Tests:
 * - Parsing: void elements (<br>, <img>) and omitted end tags (<li>, <dd>) keep <state>/<script>
 * - Text: headings, inline formatting, entities, whitespace, <br>, <pre>, <xmp>, quotes, ruby
 * - Lists: bullets by depth, <ol start type reversed>, <li value>, <dl>
 * - Blocks: div/td with direct text, sections, figure, fieldset, details, dialog, table parts
 * - Forms: checkbox, radio, range, text/password/hidden/submit, textarea, progress, meter
 * - Hidden elements and fallback content (<video>, <audio>, <iframe>, unknown tags)
 * - Flow: HTML and unpositioned TelaOS widgets share one flow root; x/y widgets stay put
 * - A page without HTML builds exactly what it did before
 * - Live {var} in plain and formatted text, driven by Lua
 * - Colors: all CSS names; malformed markup (<td> without </td>, <p> before <div>, unknown tags)
 * - ui.setText() on HTML elements
 */

#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
#include "engines/lua/lua_engine.h"
#include "ui/ui_html_internal.h"

using MW = LvglMock::Widget;

static int passed = 0, total = 0;
#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while (0)

// ============ Helpers ============

static void render(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(html);
}

static MW* byId(const char* id) { return LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr; }
static MW* byText(const char* t) { return LvglMock::g_screen ? LvglMock::g_screen->findByText(t) : nullptr; }

// Shown = neither it nor an ancestor is hidden
static bool shown(const MW* w) {
    for (; w; w = w->parent) {
        if (w->hidden) return false;
    }
    return true;
}

// Text a person sees under w: label texts in tree order, joined by one space
static void collect(const MW* w, std::string& out) {
    if (w->hidden) return;
    if ((w->type == "Label" || w->type == "Checkbox") && !w->text.empty()) {
        if (!out.empty() && out.back() != ' ' && out.back() != '\n' && w->text[0] != ' ') out += ' ';
        out += w->text;
    }
    for (auto* c : w->children) collect(c, out);
}

static std::string textOf(const MW* w) {
    std::string s;
    if (w) collect(w, s);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static void expectText(const char* name, const char* id, const char* want) {
    TEST(name) {
        std::string got = textOf(byId(id));
        if (got == want) PASS();
        else FAIL(("got \"" + got + "\"").c_str());
    }
}

static void expectTrue(const char* name, bool ok, const char* why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why);
    }
}

// ============ Apps ============

static const char* APP_PARSE = R"HTML(
<app>
  <ui default="/m">
    <page id="m">
      <p id="lv">Count {count}</p>
      <p id="lv2">Count <b>{count}</b>!</p>
      <p>line<br>break</p>
      <img src="none.png">
      <hr>
      <ul id="ul"><li>one<li>two</ul>
      <input type="checkbox" id="flag" bind="flag">
      <label id="after">After the HTML</label>
    </page>
  </ui>
  <state>
    <int name="count" default="7"/>
    <string name="flag" default="true"/>
  </state>
  <script language="lua">
function bump() state.count = state.count + 1 end
  </script>
</app>
)HTML";

static const char* APP_TEXT = R"HTML(
<app><ui default="/m"><page id="m">
  <h1 id="h1">Title</h1>
  <h2 id="h2">Sub</h2>
  <h4 id="h4">Small heading</h4>
  <p id="p1">Hello <b>bold</b> and <i>italic</i>, <code>x = 1</code>.</p>
  <p id="ent">&amp; &lt;tag&gt; &copy; &#8364; &#x41;</p>
  <p id="ws">  many    spaces
     and lines  </p>
  <p id="br">one<br>two</p>
  <p id="q"><q>quoted</q> <ruby>kan<rp>(</rp><rt>ji</rt><rp>)</rp></ruby></p>
  <p id="mix"><b>b</b><i>i</i><u>u</u><s>s</s><mark>m</mark><small>sm</small><sub>2</sub><sup>3</sup></p>
  <pre id="pre">
  keep
    this</pre>
  <xmp id="xmp"><b>raw</b></xmp>
  <listing id="lst">old <b>pre</b></listing>
  <blockquote id="bq">Quote</blockquote>
  <address id="adr">Street 1</address>
  <hr id="rule">
</page></ui></app>
)HTML";

static const char* APP_LISTS = R"HTML(
<app><ui default="/m"><page id="m">
  <ul id="ul"><li>Apple<li>Pear<ul><li>Inner</ul></ul>
  <ol id="ol" start="3"><li>c</li><li>d</li></ol>
  <ol id="olr" type="I" reversed><li>x</li><li>y</li><li>z</li></ol>
  <ol id="ola" type="a"><li value="27">aa</li></ol>
  <dl id="dl"><dt>T<dd>D</dl>
  <menu id="menu"><li>item</li></menu>
</page></ui></app>
)HTML";

static const char* APP_BLOCKS = R"HTML(
<app><ui default="/m"><page id="m">
  <div id="d1">Direct text</div>
  <div id="d2"><b>A</b> <i>B</i></div>
  <section id="sec"><header>Head</header><article><p>Body</p></article><footer>Foot</footer></section>
  <details id="det"><summary>More</summary><p>Secret</p></details>
  <details id="det2" open><summary>Open</summary><p>Shown</p></details>
  <dialog id="dlg"><p>Modal</p></dialog>
  <dialog id="dlg2" open><p>Visible modal</p></dialog>
  <table id="tbl">
    <caption>Cities</caption>
    <thead><tr><th>City</th></tr></thead>
    <tbody><tr><td>Moscow</td></tr></tbody>
  </table>
  <figure id="fig"><img id="im" src="pic.png" width="40" height="30"><figcaption>Cap</figcaption></figure>
  <fieldset id="fs"><legend>Group</legend>Inside</fieldset>
</page></ui></app>
)HTML";

static const char* APP_FORMS = R"HTML(
<app><ui default="/m"><page id="m">
  <form id="form">
    <input type="checkbox" id="cb" checked>
    <input type="checkbox" id="cb2">
    <input type="radio" id="r1" name="g"> <input type="radio" id="r2" name="g" checked>
    <input type="range" id="rng" min="0" max="10" value="7">
    <input type="text" id="txt" placeholder="Name">
    <input type="password" id="pw">
    <input type="hidden" id="hid" value="x">
    <input type="submit" id="go" value="Go">
    <textarea id="ta" rows="3">Hi &amp; bye</textarea>
    <progress id="pg" value="0.25"></progress>
    <meter id="mt" min="0" max="10" value="5"></meter>
    <select id="sel"><option value="a">A</option><option value="b">B</option></select>
    <input id="tela" bind="name"/>
  </form>
</page></ui></app>
)HTML";

static const char* APP_HIDDEN = R"HTML(
<app><ui default="/m"><page id="m">
  <main id="mn">
    <template><p>tpl</p></template>
    <noscript>nos</noscript>
    <head><title>ttl</title></head>
    <video id="vid" src="a.mp4">No video</video>
    <audio id="aud" src="a.mp3"></audio>
    <iframe id="ifr" src="https://example.com" title="Site"></iframe>
    <svg id="svg"><circle r="1"/></svg>
    <blink>Unknown</blink>
    <slot>Slotted</slot>
  </main>
</page></ui></app>
)HTML";

static const char* APP_FLOW = R"HTML(
<app><ui default="/m"><page id="m">
  <!-- a comment > with a bracket -->
  <style>.hot { color: #FF0000; }</style>
  <p id="para" class="hot">Para</p>
  <label id="free">Free</label>
  <label id="pinned" x="10" y="20">Pinned</label>
  <button id="btn">Btn</button>
  Tail <b>text</b> <i>here</i>
</page></ui></app>
)HTML";

static const char* APP_EDGE = R"HTML(
<app><ui default="/m"><page id="m">
  <style>.warm { color: orange; } .leaf { color: green; }</style>
  <p id="warm" class="warm">warm</p>
  <p id="leaf" class="leaf">leaf</p>
  <table id="cells"><tr><td>a<td>b</tr><tr><td>c</td></tr></table>
  <p id="pdiv">before<div id="blk">block</div>
  <th>stray header</th>
  <my-widget>custom</my-widget> element
</page></ui></app>
)HTML";

static const char* APP_SETTEXT = R"HTML(
<app><ui default="/m"><page id="m">
  <p id="plain">old</p>
  <p id="rich">old <b>bold</b></p>
  <ul><li id="li1">old item</li></ul>
  <section id="sec"><p>a</p><p>b</p></section>
  <input type="checkbox" id="cb">
  <progress id="pg" value="0.5"></progress>
  <hr id="rule">
  <dialog id="dlg" open><p>old dialog</p></dialog>
</page></ui></app>
)HTML";

static const char* APP_TELA = R"HTML(
<app><ui default="/m"><page id="m">
  <!-- TelaOS only: nothing here may change -->
  <label id="a" x="1" y="2">A</label>
  <label id="b">B</label>
  <div id="box" w="100" h="50"><label id="c">C</label></div>
</page></ui></app>
)HTML";

// ============ Lua for live bindings ============

static LuaEngine* g_eng = nullptr;

static bool loadWithLua(LuaEngine& engine, const char* html) {
    render(html);
    engine.shutdown();
    for (int i = 0; i < g_core.stateCount(); i++) {
        const char* n = g_core.stateVarName(i);
        const char* d = g_core.stateVarDefault(i);
        if (n) engine.setState(n, d ? d : "");
    }
    engine.init();
    for (int i = 0; i < g_core.stateCount(); i++) {
        const char* n = g_core.stateVarName(i);
        const char* d = g_core.stateVarDefault(i);
        if (n) engine.setState(n, d ? d : "");
    }
    g_eng = &engine;
    g_core.setOnClickHandler([](const char* f) { if (g_eng) g_eng->call(f); });
    engine.onStateChange([](const char* k, const char* v) { ui_update_bindings(k, v); });
    const char* code = g_core.scriptCode();
    return (code && code[0]) ? engine.execute(code) : false;
}

// ============ Tests ============

int main() {
    printf("=== HTML TAGS TESTS ===\n\n");

    printf("Parsing: void elements and omitted end tags:\n");
    LuaEngine engine;
    const bool luaOk = loadWithLua(engine, APP_PARSE);
    expectTrue("<script> still found after <br>, <img>, <input>", luaOk && strstr(g_core.scriptCode(), "function bump"),
               "no script");
    expectTrue("<state> still found (count = 7)", g_core.store().getAsString("count") == "7", "no state");
    expectText("<li> without </li>", "ul", "• one • two");
    expectTrue("checkbox bound to state (flag = true)", byId("flag") && byId("flag")->checked, "unchecked");
    expectTrue("TelaOS label after the HTML is built", byId("after") != nullptr, "missing");

    printf("\nLive {var}:\n");
    expectText("plain text binding", "lv", "Count 7");
    expectText("binding inside formatted text", "lv2", "Count 7!");
    engine.call("bump");
    expectText("plain text after Lua bump()", "lv", "Count 8");
    expectText("formatted text after Lua bump()", "lv2", "Count 8!");
    engine.shutdown();
    g_eng = nullptr;

    printf("\nText:\n");
    render(APP_TEXT);
    {
        MW* h1 = byId("h1");
        MW* h2 = byId("h2");
        MW* h4 = byId("h4");
        expectTrue("h1: 48px white", h1 && h1->text == "Title" && h1->fontSize == 48 && h1->color == 0xFFFFFF);
        expectTrue("h2: 32px #DDDDDD", h2 && h2->fontSize == 32 && h2->color == 0xDDDDDD);
        expectTrue("h4: 16px", h4 && h4->fontSize == 16);
    }
    expectText("inline formatting keeps the words", "p1", "Hello bold and italic, x = 1.");
    expectText("entities", "ent", "& <tag> © € A");
    expectText("whitespace collapses", "ws", "many spaces and lines");
    expectText("<br>", "br", "one\ntwo");
    expectText("<q> quotes, <ruby> with <rt>", "q", "“quoted” kan(ji)");
    expectText("adjacent inline elements", "mix", "biusmsm23");
    expectText("<pre> keeps whitespace", "pre", "  keep\n    this");
    expectText("<xmp> shows tags literally", "xmp", "<b>raw</b>");
    expectText("<listing> parses tags, like <pre>", "lst", "old pre");
    expectText("<blockquote>", "bq", "Quote");
    expectText("<address>", "adr", "Street 1");
    expectTrue("<hr>", byId("rule") != nullptr);

    printf("\nLists:\n");
    render(APP_LISTS);
    expectText("<ul> bullets by depth", "ul", "• Apple • Pear – Inner");
    expectText("<ol start=3>", "ol", "3. c 4. d");
    expectText("<ol type=I reversed>", "olr", "III. x II. y I. z");
    expectText("<ol type=a>, <li value=27>", "ola", "aa. aa");
    expectText("<dl> with omitted end tags", "dl", "T D");
    expectText("<menu>", "menu", "• item");

    printf("\nBlocks:\n");
    render(APP_BLOCKS);
    expectText("text right inside a <div>", "d1", "Direct text");
    expectText("inline tags right inside a <div>: one line", "d2", "A B");
    expectText("sections", "sec", "Head Body Foot");
    expectTrue("<details> closed: body hidden", byText("Secret") && !shown(byText("Secret")));
    expectTrue("<details> closed: summary shown", byText("More") && shown(byText("More")));
    expectTrue("<details open>: body shown", byText("Shown") && shown(byText("Shown")));
    expectTrue("<dialog> closed: hidden", byText("Modal") && !shown(byText("Modal")));
    expectTrue("<dialog open>: shown", byText("Visible modal") && shown(byText("Visible modal")));
    expectText("table: <caption>, <thead>/<th>, <td> text", "tbl", "Cities City Moscow");
    {
        MW* im = byId("im");
        expectTrue("<img width height> -> image 40x30", im && im->w == 40 && im->h == 30);
    }
    expectText("<figure> with <figcaption>", "fig", "Cap");
    expectText("<fieldset> with <legend>", "fs", "Group Inside");

    printf("\nForms:\n");
    render(APP_FORMS);
    expectTrue("checkbox checked", byId("cb") && byId("cb")->checked);
    expectTrue("checkbox unchecked", byId("cb2") && !byId("cb2")->checked);
    expectTrue("radio checked attribute", byId("r2") && byId("r2")->checked && byId("r1") && !byId("r1")->checked);
    {
        MW* r = byId("rng");
        expectTrue("range -> slider 0..10 = 7", r && r->minValue == 0 && r->maxValue == 10 && r->curValue == 7);
    }
    expectTrue("text input placeholder", byId("txt") && byId("txt")->placeholder == "Name");
    expectTrue("password input", byId("pw") && byId("pw")->password);
    expectTrue("hidden input builds nothing", byId("hid") == nullptr);
    expectText("submit button with value", "go", "Go");
    expectTrue("textarea content", byId("ta") && byId("ta")->text == "Hi & bye");
    expectTrue("progress 0.25 -> 250/1000", byId("pg") && byId("pg")->curValue == 250);
    expectTrue("meter 5 of 0..10 -> 500/1000", byId("mt") && byId("mt")->curValue == 500);
    expectTrue("select stays TelaOS dropdown", byId("sel") && byId("sel")->type == "Dropdown");
    expectTrue("input without type= stays TelaOS input", byId("tela") && byId("tela")->type == "TextArea");

    printf("\nHidden and fallback content:\n");
    render(APP_HIDDEN);
    expectText("template/noscript/head hidden, fallbacks shown", "mn",
               "No video [audio] [iframe] Site [svg] Unknown Slotted");

    printf("\nFlow:\n");
    render(APP_FLOW);
    {
        MW* para = byId("para");
        MW* free = byId("free");
        MW* pinned = byId("pinned");
        MW* btn = byId("btn");
        MW* page = LvglMock::g_screen->first("Container");
        MW* root = para ? para->parent : nullptr;
        expectTrue("HTML goes into one flow root on the page", root && root->parent == page);
        expectTrue("TelaOS label without x/y joins the flow", free && free->parent == root);
        expectTrue("TelaOS button without x/y joins the flow", btn && btn->parent == root);
        expectTrue("TelaOS label with x/y stays on the page", pinned && pinned->parent == page);
        expectTrue("text right inside the page: one line", byText("Tail text here") != nullptr);
        expectTrue("comment with '>' leaves no text", textOf(page).find("bracket") == std::string::npos);
        expectTrue("<style> in the page applies to class", para && para->color == 0xFF0000);
    }

    printf("\nColors:\n");
    expectTrue("named color attribute: teal", parse_color("teal") == 0x008080);
    expectTrue("named colors ignore case: RebeccaPurple", parse_color("RebeccaPurple") == 0x663399);
    expectTrue("hex colors unchanged", parse_color("#123456") == 0x123456 && parse_color("#abc") == 0xAABBCC);
    expectTrue("is_color: names and hex yes, junk no",
               is_color("navy") && is_color("#fff") && is_color("#a1b2c3") && !is_color("notacolor") &&
                   !is_color("#12") && !is_color(""));

    printf("\nMalformed and edge markup:\n");
    render(APP_EDGE);
    expectTrue("CSS class with a color name (orange)", byId("warm") && byId("warm")->color == 0xFFA500);
    expectTrue("CSS green is #008000, as in browsers", byId("leaf") && byId("leaf")->color == 0x008000);
    expectText("<td> without </td> ends at the next <td>", "cells", "a b c");
    expectText("<p> ends where a <div> starts", "pdiv", "before");
    expectText("the <div> after it is its own block", "blk", "block");
    expectTrue("<th> outside a table is text", byText("stray header") != nullptr);
    expectTrue("unknown element in text shows its content", byText("custom element") != nullptr);

    printf("\nui.setText() on HTML elements:\n");
    render(APP_SETTEXT);
    g_core.setText("plain", "new");
    g_core.setText("rich", "new rich");
    g_core.setText("li1", "new item");
    g_core.setText("sec", "replaced");
    g_core.setText("cb", "Agree");
    g_core.setText("pg", "ignored");
    g_core.setText("rule", "ignored");
    g_core.setText("dlg", "new dialog");
    expectText("paragraph", "plain", "new");
    expectText("formatted paragraph", "rich", "new rich");
    expectText("<li>: text replaced, marker kept", "li1", "• new item");
    expectText("block: content replaced, like textContent", "sec", "replaced");
    expectText("checkbox label", "cb", "Agree");
    expectTrue("progress keeps its value", byId("pg") && byId("pg")->curValue == 500);
    expectTrue("<hr> takes no text", byId("rule") && byId("rule")->text.empty());
    expectTrue("<dialog>: its window gets the text", byText("new dialog") && !byText("old dialog"));

    printf("\nTelaOS page without HTML:\n");
    render(APP_TELA);
    {
        MW* page = LvglMock::g_screen->first("Container");
        expectTrue("page holds exactly its 3 widgets", page && page->children.size() == 3);
        expectTrue("labels stay on the page", byId("a") && byId("a")->parent == page && byId("b")->parent == page);
        expectTrue("div content untouched", byId("c") && byId("c")->parent == byId("box"));
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d HTML TAGS TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d HTML TAGS TESTS PASSED ===\n", passed, total);
    return 1;
}
