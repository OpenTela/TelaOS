/**
 * Test: el:addEventListener() for Lua — the event goes up the elements, stopPropagation(),
 * preventDefault() of what TelaOS does next, removeEventListener() (docs/DOM.md). The mock LVGL
 * calls the callbacks of an object as LVGL does: LV_EVENT_PREPROCESS ones first.
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
  <ui default="/main">
    <page id="main">
      <div id="card">
        <button id="b" onclick="tapped">B</button>
        <button id="inner">Inner</button>
        <label id="lbl">label</label>
        <input type="text" id="cardInput">
      </div>
      <button id="go" href="/second">Go</button>
      <input type="text" id="name">
      <slider id="vol" min="0" max="10"/>
    </page>
    <page id="second">
      <label id="s">second</label>
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

// What the listeners noted since the last time, in [] (none: [])
static void expectNoted(const char* name, const char* want) {
    expectLua(name, "return '[' .. noted() .. ']'", ("[" + std::string(want) + "]").c_str());
}

static void run(const char* code) { g_engine->execute(code); }

// An LVGL event on an element's object, as a tap or a key sends it
static void send(const char* id, lv_event_code_t code) {
    MW* w = byId(id);
    if (w && w->lv_obj) lv_obj_send_event(static_cast<lv_obj_t*>(w->lv_obj), code, nullptr);
}

static bool shown(const char* id) {  // nothing it is in is hidden: its page is the one on the screen
    const MW* w = byId(id);
    for (const MW* p = w; p; p = p->parent) {
        if (p->hidden) return false;
    }
    return w != nullptr;
}

static void render() {
    g_core.initDynamicApp(nullptr);
    g_core.render(APP);
}

int main() {
    printf("=== EVENTS FOR LUA ===\n\n");
    LvglMock::create_screen(480, 480);
    render();
    LuaEngine engine;
    engine.init();
    g_engine = &engine;
    g_core.setOnClickHandler([](const char* f) {
        if (g_engine) g_engine->call(f);
    });
    run("function el(id) return document.getElementById(id) end "
        "log = {} function note(s) log[#log + 1] = s end "
        "function noted() local s = table.concat(log, ' ') log = {} return s end "
        "function tapped() note('onclick') end");

    printf("addEventListener:\n");
    run("el('b'):addEventListener('click', function(e) "
        "note(e.type .. ':' .. e.target.id .. '/' .. e.currentTarget.id .. ' ' .. tostring(e.defaultPrevented)) end)");
    send("b", LV_EVENT_CLICKED);
    expectNoted("a click comes to the listener, then onclick= of the markup", "click:b/b false onclick");
    run("el('b'):addEventListener('click', function(e) same = e.target == el('b') and e.currentTarget == el('b') end)");
    send("b", LV_EVENT_CLICKED);
    expectLua("e.target: the same value as getElementById gives", "noted() return tostring(same)", "true");

    printf("\nUp the elements:\n");
    run("el('card'):addEventListener('click', function(e) note('card:' .. e.target.id .. '/' .. e.currentTarget.id) end)");
    send("b", LV_EVENT_CLICKED);
    expectNoted("the card gets its button's click after the button", "click:b/b false card:b/card onclick");
    send("lbl", LV_EVENT_CLICKED);
    expectNoted("...and its label's", "card:lbl/card");
    {
        const MW* w = byId("card");
        expectTrue("an element with a click listener takes taps", w && w->clickable);
    }
    run("el('inner'):addEventListener('click', function(e) note('inner') e:stopPropagation() end)");
    send("inner", LV_EVENT_CLICKED);
    expectNoted("stopPropagation(): the card does not get it", "inner");
    run("el('inner'):addEventListener('click', function(e) note('inner2') end)");
    send("inner", LV_EVENT_CLICKED);
    expectNoted("...the element's other listeners still do", "inner inner2");

    printf("\nOnce each, and off:\n");
    run("f = function() note('f') end el('lbl'):addEventListener('click', f) el('lbl'):addEventListener('click', f)");
    send("lbl", LV_EVENT_CLICKED);
    expectNoted("the same listener twice: it runs once", "f card:lbl/card");
    run("el('lbl'):removeEventListener('click', f)");
    send("lbl", LV_EVENT_CLICKED);
    expectNoted("removeEventListener", "card:lbl/card");
    run("el('lbl'):removeEventListener('click', f) el('lbl'):removeEventListener('focus', f)");
    send("lbl", LV_EVENT_CLICKED);
    expectNoted("...again, and of a type it has not: nothing changes", "card:lbl/card");
    run("g = function() note('g') el('lbl'):removeEventListener('click', g) end "
        "el('lbl'):addEventListener('click', g) el('lbl'):addEventListener('click', function() note('h') end)");
    send("lbl", LV_EVENT_CLICKED);
    expectNoted("a listener that takes itself off: the next still runs", "g h card:lbl/card");
    send("lbl", LV_EVENT_CLICKED);
    expectNoted("...and it is off", "h card:lbl/card");

    printf("\npreventDefault():\n");
    run("stay = function(e) e:preventDefault() note(tostring(e.defaultPrevented)) end "
        "el('go'):addEventListener('click', stay)");
    send("go", LV_EVENT_CLICKED);
    expectNoted("e.defaultPrevented", "true");
    expectTrue("a button's href= is not followed", shown("go") && !shown("s"));
    run("el('go'):removeEventListener('click', stay)");
    send("go", LV_EVENT_CLICKED);
    expectTrue("...without the listener, it is", shown("s") && !shown("go"));

    printf("\nTypes:\n");
    run("el('name'):addEventListener('input', function() error('boom') end) "
        "el('name'):addEventListener('input', function(e) note('input') end) "
        "el('name'):addEventListener('change', function(e) note('change') end)");
    send("name", LV_EVENT_VALUE_CHANGED);
    expectNoted("a listener that fails: the next one runs", "input");
    send("name", LV_EVENT_READY);
    expectNoted("a text field's change: when it is done (OK)", "change");
    run("el('vol'):addEventListener('input', function() note('input') end) "
        "el('vol'):addEventListener('change', function() note('change') end)");
    send("vol", LV_EVENT_VALUE_CHANGED);
    expectNoted("a slider's input: while it moves", "input");
    send("vol", LV_EVENT_RELEASED);
    expectNoted("...its change: when it is let go", "change");
    run("el('card'):addEventListener('focus', function() note('card') end) "
        "el('cardInput'):addEventListener('focus', function() note('focus') end) "
        "el('cardInput'):addEventListener('blur', function() note('blur') end)");
    send("cardInput", LV_EVENT_FOCUSED);
    send("cardInput", LV_EVENT_DEFOCUSED);
    expectNoted("focus and blur do not go up", "focus blur");
    expectLua("a type TelaOS has not: an error",
              "return pcall(function() el('b'):addEventListener('hover', function() end) end)", "false");
    expectLua("not a function: an error", "return pcall(function() el('b'):addEventListener('click', 5) end)",
              "false");

    printf("\nThe event after its listeners:\n");
    run("el('lbl'):addEventListener('click', function(e) kept = e end)");
    send("lbl", LV_EVENT_CLICKED);
    expectLua("kept: it has nothing in it", "return tostring(kept.type) .. ' ' .. tostring(kept.target)", "nil nil");
    expectLua("...and its methods do nothing", "kept:preventDefault() kept:stopPropagation() return 'ok'", "ok");

    printf("\nThe next app:\n");
    run("noted() keep = el('b')");
    render();
    expectLua("an element of the app before: addEventListener does nothing",
              "keep:addEventListener('click', function() note('old') end) return 'ok'", "ok");
    send("b", LV_EVENT_CLICKED);
    expectNoted("...and the new button has no listener of the old one", "onclick");

    engine.shutdown();
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d EVENT TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d EVENT TESTS PASSED ===\n", passed, total);
    return 1;
}
