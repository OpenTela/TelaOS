/**
 * Test: document.createElement and el:appendChild(), insertBefore(), removeChild() for Lua — the
 * element is built as markup would be, CSS where it stands, taken out and put back as it was
 * (docs/DOM.md).
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
    #list li:last-child { color: #020002; }
    .box p { color: #030003; }
  </style>
  <ui default="/main">
    <page id="main">
      <ul id="list"><li id="one">one</li><li id="two">two</li></ul>
      <div id="box" class="box"><p id="para">text</p></div>
      <div id="other"><p id="first">first</p></div>
      <label id="lbl">label</label>
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

static void run(const char* code) { g_engine->execute(code); }

static void expectColor(const char* name, const char* id, uint32_t want) {
    const MW* w = byId(id);
    expectTrue(name, w && w->hasColor && w->color == want, colorOf(w));
}

static void expectNoColor(const char* name, const char* id) {
    const MW* w = byId(id);
    expectTrue(name, w && !w->hasColor, colorOf(w));
}

static bool shown(const char* id) {  // its widget is on the screen: nothing it is in is hidden
    const MW* w = byId(id);
    for (const MW* p = w; p; p = p->parent) {
        if (p->hidden) return false;
    }
    return w != nullptr;
}

static int indexIn(const MW* w) {  // its place among its parent's widgets
    if (!w || !w->parent) return -1;
    const auto& ch = w->parent->children;
    for (size_t i = 0; i < ch.size(); ++i) {
        if (ch[i] == w) return static_cast<int>(i);
    }
    return -1;
}

static void render() {
    g_core.initDynamicApp(nullptr);
    g_core.render(APP);
}

int main() {
    printf("=== THE TREE FOR LUA ===\n\n");
    LvglMock::create_screen(480, 480);
    render();
    LuaEngine engine;
    engine.init();
    g_engine = &engine;
    run("function el(id) return document.getElementById(id) end "
        "function ids(list) local t = {} for _, e in ipairs(list) do t[#t + 1] = e.id end "
        "return table.concat(t, ' ') end");

    printf("createElement:\n");
    expectLua("an element", "fresh = document:createElement('LI') return fresh.tagName", "LI");
    expectLua("document.createElement() too", "return document.createElement('div').tagName", "DIV");
    expectLua("a name with a space: an error", "return pcall(function() document:createElement('a b') end)", "false");
    run("fresh.id = 'three' fresh.textContent = 'three' fresh.classList:add('new')");
    expectLua("out of the document: getElementById does not find it", "return el('three')", "nil");
    expectLua("...nor querySelector", "return document:querySelector('.new')", "nil");
    expectLua("its text and attributes are kept", "return fresh.textContent .. ' ' .. fresh.className", "three new");
    expectLua("no parent", "return fresh.parentElement", "nil");

    printf("\nappendChild:\n");
    expectLua("it gives the child back", "return el('list'):appendChild(fresh) == fresh", "true");
    expectLua("in the document: found, the same value", "return fresh ~= nil and el('three') == fresh", "true");
    expectLua("parentElement", "return fresh.parentElement.id", "list");
    expectLua("the page order", "return ids(el('list'):querySelectorAll('li'))", "one two three");
    {
        const MW* w = byId("three");
        expectTrue("built: its widget is on the screen", shown("three"), w ? "hidden" : "no widget");
        expectLua("...with its text", "return fresh.textContent", "three");
    }
    expectColor("CSS where it stands: li:last-child", "three", 0x020002);
    expectNoColor("...and the one that was last is not any more", "two");

    printf("\ninsertBefore:\n");
    run("zero = document:createElement('li') zero.id = 'zero' zero.textContent = 'zero' "
        "el('list'):insertBefore(zero, el('one'))");
    expectLua("before the element given", "return ids(el('list'):querySelectorAll('li'))", "zero one two three");
    {
        const MW* z = byId("zero");
        const MW* o = byId("one");
        expectTrue("...its widget too", z && o && z->parent == o->parent && indexIn(z) + 1 == indexIn(o),
                   std::to_string(indexIn(z)) + " " + std::to_string(indexIn(o)));
    }
    run("el('list'):insertBefore(el('three'), nil)");
    expectLua("nil: at the end", "return ids(el('list'):querySelectorAll('li'))", "zero one two three");
    run("el('list'):insertBefore(el('three'), el('one'))");
    expectLua("an element in the document moves", "return ids(el('list'):querySelectorAll('li'))", "zero three one two");
    {
        const MW* t = byId("three");
        const MW* o = byId("one");
        expectTrue("...its widget with it", t && o && indexIn(t) + 1 == indexIn(o),
                   std::to_string(indexIn(t)) + " " + std::to_string(indexIn(o)));
    }
    expectColor("...and CSS follows: the last one now", "two", 0x020002);
    expectNoColor("...the one moved is not", "three");

    printf("\nremoveChild:\n");
    expectLua("it gives the child back", "two = el('list'):removeChild(el('two')) return two.id", "two");
    expectLua("out of the document", "return tostring(el('two')) .. ' ' .. tostring(two.parentElement)", "nil nil");
    expectTrue("its widget is hidden", !shown("two"));
    expectLua("...and kept as it was", "return two.textContent", "two");
    expectColor("CSS: the one before is the last now", "one", 0x020002);
    run("el('other'):appendChild(two)");
    expectTrue("put back elsewhere: on the screen again", shown("two"));
    expectLua("...found", "return el('two') == two and two.parentElement.id", "other");

    printf("\nMoved, CSS goes with the place:\n");
    expectColor(".box p in the box", "para", 0x030003);
    run("el('other'):appendChild(el('para'))");
    expectNoColor("...moved out of it", "para");
    expectLua("...its place", "return el('para').parentElement.id", "other");
    run("el('box'):appendChild(el('para'))");
    expectColor("...and back", "para", 0x030003);

    printf("\nA tree made apart, then put in:\n");
    run("d = document:createElement('div') d.className = 'box' "
        "p = document:createElement('p') p.id = 'built' p.textContent = 'built' d:appendChild(p) "
        "b = document:createElement('p') b.id = 'brace' b.textContent = '{result}' d:appendChild(b)");
    expectLua("in an element out of the document: still out", "return tostring(el('built')) .. ' ' .. p.parentElement.className",
              "nil box");
    run("el('other'):insertBefore(d, el('first'))");
    expectLua("in with it, before the element given", "return el('built') == p and ids(el('other'):querySelectorAll('p'))",
              "built brace first");
    expectTrue("built", shown("built"));
    expectColor("...CSS of its new ancestors: .box p", "built", 0x030003);
    expectLua("text is text: {result} is not a binding", "return el('brace').textContent", "{result}");

    printf("\nWhat cannot be:\n");
    expectLua("into itself", "return pcall(function() el('list'):appendChild(el('list')) end)", "false");
    expectLua("into what is in it", "return pcall(function() el('one'):appendChild(el('list')) end)", "false");
    expectLua("removeChild of a child of another",
              "return pcall(function() el('list'):removeChild(el('para')) end)", "false");
    expectLua("insertBefore an element that is not a child",
              "return pcall(function() el('list'):insertBefore(document:createElement('li'), el('para')) end)",
              "false");
    expectLua("a page stays where it is", "return pcall(function() el('box'):appendChild(el('main')) end)", "false");
    expectLua("into an element that shows text",
              "return pcall(function() el('lbl'):appendChild(document:createElement('b')) end)", "false");
    expectLua("the message says why",
              "local ok, e = pcall(function() el('lbl'):appendChild(document:createElement('b')) end) "
              "return tostring(e:find('shows text', 1, true) ~= nil)",
              "true");

    printf("\nEvents:\n");
    run("log = {} function note(s) log[#log + 1] = s end "
        "nb = document:createElement('button') nb.id = 'nb' nb.textContent = 'New' "
        "nb:addEventListener('click', function() note('nb') end) "
        "el('other'):addEventListener('click', function() note('other') end) el('other'):appendChild(nb)");
    {
        MW* w = byId("nb");
        if (w && w->lv_obj) lv_obj_send_event(static_cast<lv_obj_t*>(w->lv_obj), LV_EVENT_CLICKED, nullptr);
    }
    expectLua("a listener added before it was built, and the parent's", "return table.concat(log, ' ')", "nb other");

    printf("\nLet go:\n");
    run("el('other'):removeChild(el('built').parentElement) d = nil p = nil b = nil collectgarbage() collectgarbage()");
    expectTrue("taken out and held by no script: its widgets go", !byId("built") && !byId("brace"));

    printf("\nThe next app:\n");
    run("keep = el('list')");
    render();
    expectLua("an element of the app before: nothing changes, no error",
              "keep:appendChild(document:createElement('li')) return ids(el('list'):querySelectorAll('li'))",
              "one two");

    engine.shutdown();
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TREE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d TREE TESTS PASSED ===\n", passed, total);
    return 1;
}
