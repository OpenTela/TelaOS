/**
 * Test: the DOM for Lua — document.getElementById, textContent, getAttribute, setAttribute,
 * removeAttribute (docs/DOM.md). Each element kind the page walkers build is found and read.
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
      <label id="title" class="big" data-x="a &amp; b">Hello</label>
      <label id="live">{count}</label>
      <button id="go" onclick="noop">Go</button>
      <button id="rb" onclick="noop">Go <b>now</b></button>
      <label id="rich">Name <i id="opt">(optional)</i></label>
      <p id="para">Some <b id="bold">bold</b> text</p>
      <p id="total">Total: {count}</p>
      <ul id="list"><li id="item">First</li><li>Second</li></ul>
      <div id="sec"><p id="inner">Inner</p><label id="lbl2">Two</label></div>
      <section id="box"><p>Count {count}</p></section>
      <div id="dbox"><p>Left {count}</p></div>
      <details id="det"><summary id="sum">More</summary><p id="detp">Hidden text</p></details>
      <dialog id="dlg" open><p>In a dialog</p></dialog>
      <input type="text" id="name" placeholder="Name">
      <input type="checkbox" id="cb">
      <input type="radio" name="r" id="r1" checked>
      <input type="radio" name="r" id="r2">
      <input type="range" id="vol" min="0" max="10" value="3">
      <input type="submit" id="send" value="Send">
      <textarea id="note">x</textarea>
      <img id="pic" src="pic.png" alt="A picture">
      <switch id="sw"/>
      <table id="tbl"><tr><th id="th1">Head</th><td id="td1">Cell</td></tr></table>
      <tabs id="tb"><tab title="A"><label id="intab">In a tab</label></tab><tab title="B"><label id="intab2">B side</label></tab></tabs>
    </page>
    <page id="second">
      <label id="other">Other</label>
    </page>
  </ui>
  <state>
    <int name="count" default="1"/>
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

static void expectTrue(const char* name, bool ok, const std::string& why = "") {
    TEST(name) {
        if (ok) PASS();
        else FAIL(why.c_str());
    }
}

// Runs Lua that sets `r`; it is compared as a string
static void expectLua(const char* name, const char* code, const char* want) {
    g_core.store().setString("result", "");
    const std::string chunk = std::string("local r = (function() ") + code + " end)()\nstate.result = tostring(r)";
    g_engine->execute(chunk.c_str());
    const P::String got = g_core.store().getString("result");
    expectTrue(name, got == want, "got \"" + std::string(got.c_str()) + "\"");
}

static void render() {
    g_core.initDynamicApp(nullptr);
    g_core.render(APP);
}

int main() {
    printf("=== DOM FOR LUA ===\n\n");
    LvglMock::create_screen(480, 480);
    render();
    LuaEngine engine;
    engine.init();
    g_engine = &engine;

    printf("document.getElementById:\n");
    expectLua("document is a table", "return type(document)", "table");
    expectLua("finds a label", "return document.getElementById('title').tagName", "LABEL");
    expectLua("document:getElementById() too", "return document:getElementById('title').tagName", "LABEL");
    expectLua("the same element is the same value",
              "return document.getElementById('title') == document:getElementById('title')", "true");
    expectLua("an id nothing has: nil", "return document.getElementById('nope')", "nil");
    expectLua("an empty id: nil", "return document.getElementById('')", "nil");
    expectLua("the other page's elements too", "return document.getElementById('other').textContent", "Other");
    expectLua("a page", "return document.getElementById('second').tagName", "PAGE");

    printf("\ntagName, id:\n");
    expectLua("<p>", "return document.getElementById('para').tagName", "P");
    expectLua("<b> in a paragraph", "return document.getElementById('bold').tagName", "B");
    expectLua("<img> stays IMG", "return document.getElementById('pic').tagName", "IMG");
    expectLua("<input type=range> stays INPUT", "return document.getElementById('vol').tagName", "INPUT");
    expectLua("<input type=submit> stays INPUT", "return document.getElementById('send').tagName", "INPUT");
    expectLua("<textarea>", "return document.getElementById('note').tagName", "TEXTAREA");
    expectLua("<th> in a TelaOS row", "return document.getElementById('th1').tagName", "TH");
    expectLua("<tabs>", "return document.getElementById('tb').tagName", "TABS");
    expectLua("el.id", "return document.getElementById('title').id", "title");
    expectLua("tostring()", "return tostring(document.getElementById('title'))", "<LABEL id=\"title\">");

    printf("\ntextContent, read:\n");
    expectLua("a label", "return document.getElementById('title').textContent", "Hello");
    expectLua("a label with {var}: its value", "return document.getElementById('live').textContent", "1");
    expectLua("a button", "return document.getElementById('go').textContent", "Go");
    expectLua("a button with HTML in it", "return document.getElementById('rb').textContent", "Go now");
    expectLua("a label with HTML in it", "return document.getElementById('rich').textContent", "Name (optional)");
    expectLua("a paragraph with <b>", "return document.getElementById('para').textContent", "Some bold text");
    expectLua("<li>: without its marker", "return document.getElementById('item').textContent", "First");
    expectLua("a list: its items", "return document.getElementById('list').textContent", "FirstSecond");
    expectLua("a div: its children in order", "return document.getElementById('sec').textContent", "InnerTwo");
    expectLua("<details>: summary and body, no marker", "return document.getElementById('det').textContent",
              "MoreHidden text");
    expectLua("<summary>", "return document.getElementById('sum').textContent", "More");
    expectLua("<dialog>", "return document.getElementById('dlg').textContent", "In a dialog");
    expectLua("<th>", "return document.getElementById('th1').textContent", "Head");
    expectLua("<td>", "return document.getElementById('td1').textContent", "Cell");
    expectLua("a label in a tab", "return document.getElementById('intab2').textContent", "B side");
    expectLua("<input>: none", "return document.getElementById('name').textContent", "");
    expectLua("a checkbox: none", "return document.getElementById('cb').textContent", "");
    expectLua("<img>: none, not its alt", "return document.getElementById('pic').textContent", "");
    expectLua("<b> inside text: no object, empty", "return document.getElementById('bold').textContent", "");

    printf("\ngetAttribute, hasAttribute:\n");
    expectLua("class", "return document.getElementById('title'):getAttribute('class')", "big");
    expectLua("decoded: &amp;", "return document.getElementById('title'):getAttribute('data-x')", "a & b");
    expectLua("names are case-blind", "return document.getElementById('title'):getAttribute('CLASS')", "big");
    expectLua("one it has not: nil", "return document.getElementById('title'):getAttribute('title')", "nil");
    expectLua("a bare one: \"\"", "return document.getElementById('r1'):getAttribute('checked')", "");
    expectLua("<input type=range> keeps its own", "return document.getElementById('vol'):getAttribute('type')", "range");
    expectLua("<img> keeps src", "return document.getElementById('pic'):getAttribute('src')", "pic.png");
    expectLua("hasAttribute", "local e = document.getElementById('title') return tostring(e:hasAttribute('class')) .. tostring(e:hasAttribute('nope'))",
              "truefalse");

    printf("\nsetAttribute, removeAttribute:\n");
    expectLua("a new one reads back, quotes and & too",
              "local e = document.getElementById('title') e:setAttribute('data-y', 'say \"hi\" & bye') return e:getAttribute('data-y')",
              "say \"hi\" & bye");
    expectLua("a number turns into text",
              "local e = document.getElementById('title') e:setAttribute('data-n', 5) return e:getAttribute('data-n')", "5");
    expectLua("an existing one changes in place",
              "local e = document.getElementById('title') e:setAttribute('class', 'small') return e:getAttribute('class')",
              "small");
    expectLua("removeAttribute",
              "local e = document.getElementById('title') e:removeAttribute('data-x') return e:hasAttribute('data-x')",
              "false");
    expectLua("not a name: an error",
              "return pcall(function() document.getElementById('title'):setAttribute('a b', 'c') end)", "false");
    {
        g_engine->execute("document.getElementById('title'):setAttribute('hidden', '')");
        const MW* w = byId("title");
        expectTrue("hidden hides", w && w->hidden);
        g_engine->execute("document.getElementById('title'):removeAttribute('hidden')");
        expectTrue("...and taken off, shows", w && !w->hidden);
        g_engine->execute("document.getElementById('title'):setAttribute('hidden', 'false')");
        expectTrue("hidden=\"false\" shows, as in TelaOS", w && !w->hidden);
    }
    {
        g_engine->execute("document.getElementById('go'):setAttribute('disabled', '')");
        const MW* w = byId("go");
        expectTrue("disabled: the button", w && w->type == "Button" && w->disabled, w ? w->type : "no widget");
        g_engine->execute("document.getElementById('go'):removeAttribute('disabled')");
        expectTrue("...and taken off", w && !w->disabled);
    }
    {
        g_engine->execute("document.getElementById('cb'):setAttribute('checked', '')");
        const MW* cb = byId("cb");
        expectTrue("checked: a checkbox", cb && cb->checked);
        g_engine->execute("document.getElementById('r2'):setAttribute('checked', '')");
        const MW* r1 = byId("r1");
        const MW* r2 = byId("r2");
        expectTrue("checked: a radio turns its group off", r1 && r2 && r2->checked && !r1->checked);
        g_engine->execute("document.getElementById('sw'):setAttribute('checked', '')");
        const MW* sw = byId("sw");
        expectTrue("checked: a switch", sw && sw->checked);
    }
    {
        g_engine->execute("document.getElementById('name'):setAttribute('value', 'Ann')");
        const MW* w = byId("name");
        expectTrue("value: a text field", w && w->text == "Ann", w ? w->text : "no widget");
        g_engine->execute("document.getElementById('name'):setAttribute('placeholder', 'Your name')");
        expectTrue("placeholder", w && w->placeholder == "Your name", w ? w->placeholder : "no widget");
        g_engine->execute("document.getElementById('vol'):setAttribute('value', '7')");
        const MW* v = byId("vol");
        expectTrue("value: a slider", v && v->curValue == 7, v ? std::to_string(v->curValue) : "no widget");
    }
    {
        g_engine->execute("document.getElementById('dlg'):removeAttribute('open')");
        const MW* w = byId("dlg");
        expectTrue("a dialog closes without open", w && w->hidden);
        g_engine->execute("document.getElementById('dlg'):setAttribute('open', '')");
        expectTrue("...and opens with it", w && !w->hidden);
    }
    {
        g_engine->execute("document.getElementById('sec'):setAttribute('bgcolor', '#FF0000')");
        const MW* w = byId("sec");
        expectTrue("bgcolor, as ui.setAttr", w && w->hasBgcolor && w->bgcolor == 0xFF0000);
    }
    expectLua("id: found by the new one, not the old",
              "local e = document.getElementById('lbl2') e:setAttribute('id', 'two') "
              "return tostring(document.getElementById('lbl2')) .. ' ' .. tostring(document.getElementById('two') == e)",
              "nil true");
    expectLua("el.id = ... sets it", "local e = document.getElementById('two') e.id = 'deux' return e:getAttribute('id')",
              "deux");
    expectLua("ui.setAttr finds it by the new id too",
              "ui.setAttr('deux', 'text', 'Via ui') return document.getElementById('deux').textContent", "Via ui");
    expectLua("an unknown property is nil", "return document.getElementById('title').nothing", "nil");
    expectLua("...and setting one is an error",
              "return pcall(function() document.getElementById('title').nothing = 1 end)", "false");

    printf("\ntextContent, write:\n");
    expectLua("a label", "local e = document.getElementById('title') e.textContent = 'Bye' return e.textContent", "Bye");
    expectLua("a button: its label", "local e = document.getElementById('go') e.textContent = 'Stop' return e.textContent",
              "Stop");
    expectLua("a button with HTML: its text", "local e = document.getElementById('rb') e.textContent = 'Later' return e.textContent",
              "Later");
    expectLua("a paragraph: the <b> in it is gone",
              "local e = document.getElementById('para') e.textContent = 'Plain' "
              "return e.textContent .. ' ' .. tostring(document.getElementById('bold'))",
              "Plain nil");
    expectLua("<li>: the marker stays",
              "local e = document.getElementById('item') e.textContent = 'Changed' return e.textContent", "Changed");
    {
        const MW* row = byId("item");
        bool marker = false;
        if (row) {
            for (const MW* c : row->children) marker = marker || (c->type == "Label" && c->text == "\xE2\x80\xA2");
        }
        expectTrue("...its bullet is still there", marker);
    }
    expectLua("a div: what was in it is gone",
              "local inner = document.getElementById('inner') local e = document.getElementById('sec') e.textContent = 'Gone' "
              "return e.textContent .. ' ' .. tostring(document.getElementById('inner')) .. ' [' .. inner.textContent .. ']'",
              "Gone nil []");
    expectLua("nil empties it", "local e = document.getElementById('title') e.textContent = nil return '[' .. e.textContent .. ']'",
              "[]");
    {
        g_engine->execute("document.getElementById('name').textContent = 'x'");
        const MW* w = byId("name");
        expectTrue("<input>: nothing", w && w->text == "Ann", w ? w->text : "no widget");
    }
    {
        g_engine->execute("document.getElementById('live').textContent = 'fixed'");
        g_engine->execute("document.getElementById('total').textContent = 'no total'");
        ui_update_bindings("count", "5");
        const MW* live = byId("live");
        const MW* tot = byId("total");
        expectTrue("text put there stays: {count} no longer writes it", live && live->text == "fixed",
                   live ? live->text : "no widget");
        expectTrue("...in a paragraph too", tot && tot->text == "no total", tot ? tot->text : "no widget");
    }
    {
        // the {count} label in the box is deleted: bindings must not reach it any more
        const auto reaches = [](const char* tpl) {
            for (const auto& el : g_core.app().elements) {
                if (el && el->obj() && el->tpl == tpl) return true;
            }
            return false;
        };
        expectTrue("(a {count} label is bound)", reaches("Left {count}") && reaches("Count {count}"));
        g_engine->execute("document.getElementById('dbox').textContent = 'Gone'");
        expectTrue("textContent on a box: bindings let its old content go", !reaches("Left {count}"));
        g_core.setText("box", "Replaced");  // ui.setText on an HTML block
        expectTrue("ui.setText on a block: the same", !reaches("Count {count}"));
        ui_update_bindings("count", "6");
        const MW* box = byId("box");
        expectTrue("...and the block keeps the text", box && box->children.size() == 1 && box->children[0]->text == "Replaced");
    }

    printf("\nA new build:\n");
    g_engine->execute("keep = document.getElementById('title')");
    render();
    expectLua("an element of the app before finds nothing",
              "return '[' .. keep.textContent .. '] ' .. tostring(keep:getAttribute('id')) .. ' ' .. "
              "tostring(document.getElementById('title') == keep)",
              "[] nil false");
    expectLua("the new one is there", "return document.getElementById('title').textContent", "Hello");

    engine.shutdown();
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d DOM TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d of %d DOM TESTS PASSED ===\n", passed, total);
    return 1;
}
