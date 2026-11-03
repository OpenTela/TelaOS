/**
 * Test: document.querySelector and querySelectorAll for Lua, el:querySelector() too — the
 * selectors of CSS over the element tree, in the page order (docs/DOM.md).
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

static const char* APP = R"HTML(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <div id="menu" class="menu">
        <label id="m1" class="item">one</label>
        <label id="m2" class="item on">two</label>
        <div id="sub" class="sub">
          <label id="m3" class="item">three</label>
          <p id="deep">Some <b id="bold">bold</b> text</p>
        </div>
      </div>
      <ul id="ul"><li id="l1">a</li><li id="l2" class="x">b</li><li id="l3">c</li><li id="l4">d</li></ul>
      <h2 id="h">Title</h2>
      <p id="afterH">after</p>
      <p id="later">later</p>
      <input type="checkbox" id="c1" checked>
      <input type="checkbox" id="c2">
      <input type="radio" name="r" id="r1" checked>
      <input type="radio" name="r" id="r2">
      <input type="text" id="name" placeholder="Name">
      <button id="b1" disabled onclick="noop">B1</button>
      <button id="b2" onclick="noop">B2</button>
      <label id="tpl" class="{cls}">template</label>
      <label id="data" data-tone="hot" data-n="12">data</label>
      <table id="tbl"><tr><th id="th1">Head</th><td id="td1">Cell</td></tr></table>
      <div id="gonebox"><label id="gone" class="item">gone</label></div>
    </page>
    <page id="second">
      <label id="s1" class="item">on page two</label>
    </page>
  </ui>
  <state>
    <string name="cls" default="tplclass"/>
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

// The ids document:querySelectorAll(sel) finds, in order, joined by spaces (in [], so that
// nothing found is not an error)
static void expectAll(const char* name, const char* sel, const char* want) {
    const std::string code = std::string("return '[' .. ids(document:querySelectorAll('") + sel + "')) .. ']'";
    expectLua(name, code.c_str(), ("[" + std::string(want) + "]").c_str());
}

static void run(const char* code) { g_engine->execute(code); }

static void render() {
    g_core.initDynamicApp(nullptr);
    g_core.render(APP);
}

int main() {
    printf("=== QUERYSELECTOR FOR LUA ===\n\n");
    LvglMock::create_screen(480, 480);
    render();
    LuaEngine engine;
    engine.init();
    g_engine = &engine;
    run("function ids(list) local t = {} for _, e in ipairs(list) do t[#t + 1] = e.id end "
        "return table.concat(t, ' ') end");

    printf("querySelector:\n");
    expectLua("the first in the page order", "return document:querySelector('.item').id", "m1");
    expectLua("document.querySelector() too", "return document.querySelector('.item').id", "m1");
    expectLua("the same element as getElementById",
              "return document:querySelector('#m2') == document.getElementById('m2')", "true");
    expectLua("a compound", "return document:querySelector('label.item.on').id", "m2");
    expectLua("an attribute", "return document:querySelector('[data-tone=hot]').id", "data");
    expectLua("[a^=v]", "return document:querySelector('[data-n^=\"1\"]').id", "data");
    expectLua("a type in any case", "return document:querySelector('LABEL').id", "m1");
    expectLua("nothing matches: nil", "return document:querySelector('.nope')", "nil");
    expectLua("<b> in a paragraph", "return document:querySelector('p > b').id", "bold");
    expectLua("<th>: as the script sees it", "return document:querySelector('th').id", "th1");
    expectLua("a page", "return document:querySelector('page').id", "main");
    expectLua("::placeholder is no element: nil", "return document:querySelector('input::placeholder')", "nil");

    printf("\nquerySelectorAll:\n");
    expectLua("a Lua array", "local l = document:querySelectorAll('li') return #l .. ' ' .. l[1].id", "4 l1");
    expectLua("nothing: an empty one", "return #document:querySelectorAll('.nope')", "0");
    expectAll("every page", ".item", "m1 m2 m3 gone s1");
    expectAll("a list: in the page order, each once", "#l3, #l1, li#l3", "l1 l3");
    expectAll("the pages", "page", "main second");
    expectAll("descendant", ".menu .item", "m1 m2 m3");
    expectAll("child", ".menu > .item", "m1 m2");
    expectAll("any in", "#ul *", "l1 l2 l3 l4");
    expectAll("next sibling", "h2 + p", "afterH");
    expectAll("later siblings", "h2 ~ p", "afterH later");
    expectAll("...of a class", ".x ~ li", "l3 l4");
    expectAll(":first-child", "li:first-child", "l1");
    expectAll(":nth-child(2n)", "li:nth-child(2n)", "l2 l4");
    expectAll(":nth-last-child(2)", "#ul > :nth-last-child(2)", "l3");
    expectAll(":only-child, the page's too", ".item:only-child", "gone s1");
    expectAll(":not()", "li:not(.x)", "l1 l3 l4");
    expectAll(":is()", ":is(h2, #later)", "h later");
    expectAll("th + td", "th + td", "td1");

    printf("\nInside an element:\n");
    expectLua("only what is in it",
              "return ids(document.getElementById('menu'):querySelectorAll('.item'))", "m1 m2 m3");
    expectLua("not the element itself", "return document.getElementById('menu'):querySelector('.menu')", "nil");
    expectLua("its ancestors count: .menu .item from #sub",
              "return ids(document.getElementById('sub'):querySelectorAll('.menu .item'))", "m3");
    expectLua("...and itself as one: div label",
              "return ids(document.getElementById('sub'):querySelectorAll('div label'))", "m3");
    expectLua("the first", "return document.getElementById('sub'):querySelector('*').id", "m3");
    expectLua("el.querySelector(s) with a dot: an error",
              "return pcall(function() return document.getElementById('menu').querySelector('.item') end)", "false");

    printf("\nStates as they are now:\n");
    expectAll(":checked", "input:checked", "c1 r1");
    run("document.getElementById('r2'):setAttribute('checked', '')");
    expectAll("a radio turned its group off: :checked says so", "input[type=radio]:checked", "r2");
    expectAll("...while [checked] is the attribute", "input[type=radio][checked]", "r1 r2");
    run("document.getElementById('c1'):removeAttribute('checked')");
    expectAll("unchecked", "input[type=checkbox]:checked", "");
    expectAll(":disabled", "button:disabled", "b1");
    run("document.getElementById('b1'):removeAttribute('disabled') "
        "document.getElementById('b2'):setAttribute('disabled', '')");
    expectAll("...after setAttribute and removeAttribute", "button:disabled", "b2");

    printf("\nWhat changed:\n");
    run("document.getElementById('m1').classList:add('on')");
    expectAll("a class added", ".on", "m1 m2");
    expectAll("class=\"{var}\": as rendered", ".tplclass", "tpl");
    run("document.getElementById('gonebox').textContent = 'replaced'");
    expectAll("text in place of the content: what was in it is gone", ".item", "m1 m2 m3 s1");
    expectLua("...nothing left in the box", "return document.getElementById('gonebox'):querySelector('*')", "nil");

    printf("\nSelectors that do not read:\n");
    expectLua("\"\": an error", "return pcall(function() return document:querySelector('') end)", "false");
    expectLua("a[: an error", "return pcall(function() return document:querySelector('a[') end)", "false");
    expectLua("one in the list: an error", "return pcall(function() return document:querySelectorAll('li, ') end)",
              "false");
    expectLua("::before: an error", "return pcall(function() return document:querySelector('p::before') end)", "false");
    expectLua("a state in :not(): an error, as in CSS",
              "return pcall(function() return document:querySelector('input:not(:checked)') end)", "false");
    expectLua("the message says which",
              "local ok, e = pcall(function() return document:querySelector('a[') end) "
              "return tostring(e:find(\"querySelector: 'a[' is not a selector\", 1, true) ~= nil)",
              "true");

    printf("\nThe next app:\n");
    run("keep = document.getElementById('menu')");
    render();
    expectLua("an element of the app before finds nothing",
              "return tostring(keep:querySelector('*')) .. ' ' .. #keep:querySelectorAll('*')", "nil 0");
    expectAll("the new app's elements", ".item", "m1 m2 m3 gone s1");

    engine.shutdown();
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d QUERYSELECTOR TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d QUERYSELECTOR TESTS PASSED ===\n", passed, total);
    return 1;
}
