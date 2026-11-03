/**
 * Test: el.style for Lua — its properties, cssText, setProperty and the others — and style=""
 * of a TelaOS widget, which CSS takes as it takes an HTML element's (docs/DOM.md, docs/CSS.md).
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
#include "engines/lua/lua_engine.h"
#include "ui/ui_task.h"

using MW = LvglMock::Widget;

static const char* APP = R"HTML(
<app os="1.0">
  <style>
    .on { color: #010001; }
    .imp { color: #020002 !important; }
    .acc { color: var(--accent, #000001); }
  </style>
  <ui default="/main">
    <page id="main">
      <label id="a">a</label>
      <label id="marked" style="color: #030003">style="" of a label</label>
      <div id="box" style="border: 2px solid #0E000E"><label>in a box</label></div>
      <p id="para" style="color: #040004">a paragraph</p>
      <label id="cls" class="on">class</label>
      <label id="imp" class="imp">important</label>
      <label id="h">hide me</label>
      <div id="host"><label id="inhost" class="acc">var</label></div>
      <img id="pic" src="pic.png">
    </page>
  </ui>
  <state>
    <string name="result" default=""/>
  </state>
</app>
)HTML";

static int total = 0, passed = 0;

#define TEST(name) printf("  %-60s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while (0)
#define FAIL(msg) printf("✗ %s\n", msg)

static LuaEngine* g_engine = nullptr;

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

// Runs Lua that returns a value; it is compared as a string
static void expectLua(const char* name, const char* code, const char* want) {
    g_core.store().setString("result", "");
    const std::string chunk = std::string("local r = (function() ") + code + " end)()\nstate.result = tostring(r)";
    g_engine->execute(chunk.c_str());
    const P::String got = g_core.store().getString("result");
    expectTrue(name, got == want, "got \"" + std::string(got.c_str()) + "\"");
}

// style="" of an element, in [] (none: [nil])
static void expectStyle(const char* name, const char* id, const char* want) {
    const std::string code = std::string("return '[' .. tostring(document.getElementById('") + id +
                             "'):getAttribute('style')) .. ']'";
    expectLua(name, code.c_str(), ("[" + std::string(want) + "]").c_str());
}

static void run(const char* code) { g_engine->execute(code); }

static void expectColor(const char* name, const char* id, uint32_t want) {
    const MW* w = byId(id);
    expectTrue(name, w && w->hasColor && w->color == want, colorOf(w));
}

static void expectNoColor(const char* name, const char* id) {
    const MW* w = byId(id);
    expectTrue(name, w && !w->hasColor, colorOf(w));
}

static void render() {
    g_core.initDynamicApp(nullptr);
    g_core.render(APP);
}

int main() {
    printf("=== EL.STYLE FOR LUA ===\n\n");
    LvglMock::create_screen(480, 480);
    render();
    LuaEngine engine;
    engine.init();
    g_engine = &engine;
    run("function el(id) return document.getElementById(id) end");

    printf("style=\"\" in the markup:\n");
    expectColor("a TelaOS <label>: CSS takes it", "marked", 0x030003);
    {
        const MW* w = byId("box");
        expectTrue("a TelaOS <div>", w && w->borderWidth == 2 && w->borderColor == 0x0E000E,
                   w ? std::to_string(w->borderWidth) + " " + hex(w->borderColor) : "no widget");
    }
    expectColor("an HTML <p>, as before", "para", 0x040004);
    expectLua("el.style reads it", "return el('marked').style.color", "#030003");

    printf("\nel.style:\n");
    run("el('a').style.color = '#050005'");
    expectColor("el.style.color = ...", "a", 0x050005);
    expectStyle("...written to style=\"\" as a browser writes it", "a", "color: #050005;");
    expectLua("...and read back", "return el('a').style.color", "#050005");
    run("el('a').style.backgroundColor = '#060006'");
    {
        const MW* w = byId("a");
        expectTrue("backgroundColor is background-color", w && w->bgcolor == 0x060006, w ? hex(w->bgcolor) : "no widget");
    }
    expectStyle("...after the ones before", "a", "color: #050005; background-color: #060006;");
    expectLua("el.style['background-color'] too", "return el('a').style['background-color']", "#060006");
    expectLua("a property style=\"\" has not: \"\"", "return '[' .. el('a').style.width .. ']'", "[]");
    run("el('a').style.color = ''");
    expectStyle("\"\" takes it off", "a", "background-color: #060006;");
    expectNoColor("...and what it set goes", "a");
    run("el('a').style.backgroundColor = nil");
    expectStyle("nil too", "a", "");
    run("el('a').style.color = '#0F000F' el('a').style.color = '#050005'");
    expectStyle("set again: in its place, once", "a", "color: #050005;");
    expectLua("tostring(el.style)", "return tostring(el('a').style)", "color: #050005;");

    printf("\ncssText:\n");
    run("el('a').style.cssText = 'color:#070007;;  width : 40px'");
    expectStyle("cssText = ...: written as a browser writes it", "a", "color: #070007; width: 40px;");
    expectColor("...and applied", "a", 0x070007);
    expectLua("cssText reads it", "return el('a').style.cssText", "color: #070007; width: 40px;");
    run("el('a').style = 'color: #080008'");
    expectStyle("el.style = ...: the same as cssText", "a", "color: #080008;");
    expectColor("...applied", "a", 0x080008);

    printf("\nThe cascade:\n");
    run("el('cls').style.color = '#090009'");
    expectColor("style=\"\" after the sheets", "cls", 0x090009);
    run("el('cls').style.color = ''");
    expectColor("...and the class back", "cls", 0x010001);
    run("el('imp').style.color = '#0A000A'");
    expectColor("a sheet's !important wins", "imp", 0x020002);
    run("el('imp').style:setProperty('color', '#0B000B', 'important')");
    expectColor("...style !important wins over it", "imp", 0x0B000B);
    expectStyle("...written with !important", "imp", "color: #0B000B !important;");
    expectLua("getPropertyPriority", "return el('imp').style:getPropertyPriority('color')", "important");
    expectLua("getPropertyValue", "return el('imp').style:getPropertyValue('color')", "#0B000B");
    expectLua("removeProperty gives the value back", "return el('imp').style:removeProperty('color')", "#0B000B");
    expectColor("...and the class is back", "imp", 0x020002);
    run("el('h').style.display = 'none'");
    {
        const MW* w = byId("h");
        expectTrue("display: none hides", w && w->hidden);
        run("el('h').style.display = ''");
        expectTrue("...and taken off, shows", w && !w->hidden);
    }
    run("el('host').style:setProperty('--accent', '#0D000D')");
    expectColor("--x: what is in the element takes it", "inhost", 0x0D000D);
    run("el('host').style:removeProperty('--accent')");
    expectColor("...and the fallback after", "inhost", 0x000001);

    printf("\nWhat is left alone, as in a browser:\n");
    run("el('a').style.color = '#050005; width: 10px'");
    expectStyle("a value that is two declarations", "a", "color: #080008;");
    run("el('a').style.color = 'red !important'");
    expectStyle("!important in a value", "a", "color: #080008;");
    run("el('a').style:setProperty('a b', 'x')");
    expectStyle("a name with a space", "a", "color: #080008;");
    run("el('a').style:setProperty('color', '#010101', 'urgent')");
    expectStyle("a priority that is not important", "a", "color: #080008;");
    run("el('a').style:removeProperty('width')");
    expectStyle("removeProperty of one it has not", "a", "color: #080008;");
    run("el('pic').style.width = '10px'");
    expectStyle("<img>: kept, CSS does not take it", "pic", "width: 10px;");

    printf("\nThe next app:\n");
    run("keep = el('a')");
    render();
    expectLua("an element of the app before: \"\", nothing changes",
              "keep.style.color = 'red' return '[' .. keep.style.color .. keep.style.cssText .. ']'", "[]");
    expectColor("the new app's elements are as the markup says", "marked", 0x030003);

    engine.shutdown();
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d STYLE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d STYLE TESTS PASSED ===\n", passed, total);
    return 1;
}
