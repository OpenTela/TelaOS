/**
 * Test: el.classList for Lua — add, remove, toggle, contains, el.className — and CSS applied
 * again when a class, an id or an attribute a selector looks at changes (docs/DOM.md).
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
    .hide { display: none; }
    .dark p { color: #030003; }
    .lead + label { color: #040004; }
    .list li:first-child { color: #050005; }
    .err { border: 2px solid #060006; }
    .t:active { color: #070007; }
    #renamed { color: #080008; }
    [data-tone="hot"] { color: #090009; }
    .btn { background: #0A000A; }
    .sel { background: #0B000B; }
  </style>
  <ui default="/main">
    <page id="main">
      <label id="a">a</label>
      <label id="tinted" color="#123456">tinted</label>
      <label id="h">hide me</label>
      <label id="hh">hidden twice</label>
      <div id="box"><p id="inbox">in a box</p></div>
      <label id="lead">lead</label><label id="follow">follows</label>
      <ul id="ul"><li id="first">first</li><li id="second">second</li></ul>
      <label id="e">error</label>
      <label id="t">state</label>
      <label id="r">rename</label>
      <label id="d">data</label>
      <button id="b" class="btn" onclick="noop">B</button>
      <button id="b2" class="btn" onclick="noop">B2</button>
      <label id="tpl" class="{cls}">template</label>
      <label id="plain">no rules</label>
    </page>
  </ui>
  <state>
    <string name="cls" default="x"/>
    <string name="result" default=""/>
  </state>
  <script language="lua">
    function noop() end
  </script>
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

static void run(const char* code) { g_engine->execute(code); }

static void expectColor(const char* name, const char* id, uint32_t want) {
    const MW* w = byId(id);
    expectTrue(name, w && w->hasColor && w->color == want, colorOf(w));
}

static void expectNoColor(const char* name, const char* id) {
    const MW* w = byId(id);
    expectTrue(name, w && !w->hasColor, colorOf(w));
}

int main() {
    printf("=== CLASSLIST FOR LUA ===\n\n");
    LvglMock::create_screen(480, 480);
    g_core.initDynamicApp(nullptr);
    g_core.render(APP);
    LuaEngine engine;
    engine.init();
    g_engine = &engine;

    printf("add, remove, toggle, contains:\n");
    expectLua("add, then contains",
              "local c = document.getElementById('a').classList c:add('on') return c:contains('on')", "true");
    expectLua("el.className", "return document.getElementById('a').className", "on");
    expectColor("the rule of the class applies", "a", 0x010001);
    expectLua("remove", "local c = document.getElementById('a').classList c:remove('on') return c:contains('on')",
              "false");
    expectNoColor("...and what it set goes", "a");
    expectLua("toggle: on, then off",
              "local c = document.getElementById('a').classList return tostring(c:toggle('on')) .. tostring(c:toggle('on'))",
              "truefalse");
    expectLua("toggle(name, true) twice: still on",
              "local c = document.getElementById('a').classList c:toggle('on', true) return c:toggle('on', true)", "true");
    expectLua("toggle(name, false)", "return document.getElementById('a').classList:toggle('on', false)", "false");
    expectLua("several names at once",
              "local e = document.getElementById('a') e.classList:add('p', 'q') return e.className", "p q");
    expectLua("...taken off at once",
              "local e = document.getElementById('a') e.classList:remove('p', 'q') return '[' .. e.className .. ']'",
              "[]");
    expectLua("the words come out as a browser keeps them: once each",
              "local e = document.getElementById('a') e:setAttribute('class', '  x x  y ') e.classList:add('z') return e.className",
              "x y z");
    expectLua("tostring(classList)", "return tostring(document.getElementById('a').classList)", "x y z");
    expectLua("className = ... sets it", "local e = document.getElementById('a') e.className = 'only' return e:getAttribute('class')",
              "only");
    expectLua("a name with a space: an error",
              "return pcall(function() document.getElementById('a').classList:add('x y') end)", "false");
    expectLua("an empty name: an error", "return pcall(function() document.getElementById('a').classList:toggle('') end)",
              "false");
    expectLua("contains with a bad name: false", "return document.getElementById('a').classList:contains('x y')", "false");
    expectLua("a class without rules changes nothing",
              "local e = document.getElementById('plain') e.classList:add('nothing') return e.textContent", "no rules");
    expectNoColor("...no color comes", "plain");

    printf("\nCSS again: what was there before comes back:\n");
    run("document.getElementById('tinted').classList:add('on')");
    expectColor("color= of a label, then .on", "tinted", 0x010001);
    run("document.getElementById('tinted').classList:remove('on')");
    expectColor("...and back to color=", "tinted", 0x123456);
    run("document.getElementById('h').classList:add('hide')");
    {
        const MW* w = byId("h");
        expectTrue("display: none hides", w && w->hidden);
        run("document.getElementById('h').classList:remove('hide')");
        expectTrue("...and shows again", w && !w->hidden);
    }
    {
        run("local e = document.getElementById('hh') e:setAttribute('hidden', '') e.classList:add('hide') e.classList:remove('hide')");
        const MW* w = byId("hh");
        expectTrue("hidden= stays hidden when .hide goes", w && w->hidden);
    }
    run("document.getElementById('e').classList:add('err')");
    {
        const MW* w = byId("e");
        expectTrue("border: from the class", w && w->borderWidth == 2 && w->borderColor == 0x060006,
                   w ? std::to_string(w->borderWidth) + " " + hex(w->borderColor) : "no widget");
        run("document.getElementById('e').classList:remove('err')");
        expectTrue("...and no border after", w && w->borderWidth == 0, w ? std::to_string(w->borderWidth) : "no widget");
    }
    run("document.getElementById('t').classList:add('t')");
    {
        const MW* w = byId("t");
        const MW::StateStyle* st = w ? w->stateStyle("text_color", LV_STATE_PRESSED) : nullptr;
        expectTrue(":active of the class: the pressed style", st && st->value == 0x070007, st ? hex(st->value) : "no style");
        run("document.getElementById('t').classList:remove('t')");
        expectTrue("...and none after", w && !w->stateStyle("text_color", LV_STATE_PRESSED));
    }

    printf("\nCSS again: where the element stands:\n");
    run("document.getElementById('box').classList:add('dark')");
    expectColor(".dark p: what is inside", "inbox", 0x030003);
    run("document.getElementById('box').classList:remove('dark')");
    expectNoColor("...and back", "inbox");
    run("document.getElementById('lead').classList:add('lead')");
    expectColor(".lead + label: the sibling after", "follow", 0x040004);
    run("document.getElementById('lead').classList:remove('lead')");
    expectNoColor("...and back", "follow");
    run("document.getElementById('ul').classList:add('list')");
    expectColor(".list li:first-child: the first", "first", 0x050005);
    expectNoColor("...not the second", "second");

    printf("\nCSS again: id and attributes:\n");
    run("document.getElementById('r'):setAttribute('id', 'renamed')");
    expectColor("#renamed after setAttribute('id')", "renamed", 0x080008);
    run("document.getElementById('renamed').id = 'r'");
    expectNoColor("...and back", "r");
    run("document.getElementById('d'):setAttribute('data-tone', 'hot')");
    expectColor("[data-tone=hot]", "d", 0x090009);
    run("document.getElementById('d'):removeAttribute('data-tone')");
    expectNoColor("...and back", "d");

    printf("\nWhat something else set stays:\n");
    run("ui.setAttr('b', 'bgcolor', '#00FF00') document.getElementById('b').classList:add('sel')");
    {
        const MW* w = byId("b");
        expectTrue("ui.setAttr bgcolor, then a class with a background", w && w->bgcolor == 0x00FF00,
                   w ? hex(w->bgcolor) : "no widget");
        run("document.getElementById('b2').classList:add('sel')");
        const MW* w2 = byId("b2");
        expectTrue("...the same class without it", w2 && w2->bgcolor == 0x0B000B, w2 ? hex(w2->bgcolor) : "no widget");
        run("document.getElementById('b2').classList:remove('sel')");
        expectTrue("...and back to .btn", w2 && w2->bgcolor == 0x0A000A, w2 ? hex(w2->bgcolor) : "no widget");
    }
    expectLua("class=\"{var}\": its words as rendered", "return document.getElementById('tpl').classList:contains('x')", "true");
    run("document.getElementById('tpl').classList:add('on')");
    expectLua("...a change makes them the script's", "return document.getElementById('tpl').className", "x on");
    ui_update_bindings("cls", "y");
    expectLua("...{var} no longer rewrites them", "return document.getElementById('tpl').className", "x on");
    expectColor("...nor the styles", "tpl", 0x010001);

    engine.shutdown();
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CLASSLIST TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d CLASSLIST TESTS PASSED ===\n", passed, total);
    return 1;
}
