#pragma once
/**
 * dom.h — the elements of an app as its script sees them, as a browser's DOM (docs/DOM.md).
 *
 * The page walkers and the widget builders make LVGL objects straight from the markup; the tree
 * of elements is recorded on the way. An element is entered and left while its content is built
 * (UI::CssScope), and it gets its object when it is styled (Widget::applyCss). It keeps its
 * attributes as they are written: reading one decodes it, writing one also does on the screen
 * what the attribute does in the markup (hidden, disabled, checked, value...). When a class, an id
 * or another attribute a selector looks at changes, CSS is applied again (classList, el.style).
 * Scripts find elements by the selectors of CSS (querySelector).
 *
 * An element is a handle. A new build (the next app) makes new handles: the old ones find nothing.
 */

#include "lvgl.h"
#include "utils/psram_alloc.h"
#include <cstdint>
#include <functional>
#include <string_view>

namespace UI {

class CssUndo;

namespace Dom {

using Node = uint32_t;  // an element; 0: none

// ---- Recording: the build of an app's pages calls these ----

void begin();  // a build starts: the elements of the one before are forgotten
void end();    // ...and is over: what is built later (a list's rows) is not an element
void clear();  // the app is gone: its elements too

bool enter(std::string_view tag, std::string_view attrs);  // an element's content starts (false: not recorded)
void leave();                                              // ...and ends (after an enter that was)
// An element got its object. The same attributes (the same place in the text) tell that it is
// the element just entered or left, not a new one. styled: CSS is applied to it now, its
// style="" too, and keep() takes what it changed. false: not recorded
bool element(lv_obj_t* obj, std::string_view tag, std::string_view attrs, bool styled = false);
// What CSS changed on the element just styled: put back when its rules change
void keep(CssUndo&& undo);

// The content of obj's element was replaced by text (ui.setText, textContent): the elements in it
// are gone, with their objects
void contentReplaced(lv_obj_t* obj);

// While it lives, the next element is recorded as `tag` with `attrs`: the builder got rewritten
// ones (<img> is built as <image>, <input type="range"> as <slider>). CSS sees it so too, as a
// browser does (takeAs): textarea {} styles a <textarea>, input[type="text"] a text field
class As {
public:
    As(std::string_view tag, std::string_view attrs);
    ~As();
    As(const As&) = delete;
    As& operator=(const As&) = delete;

private:
    std::string_view tag_, attrs_;  // the As around this one, for CSS
    bool css_;
};

// The tag and the attributes of the markup an As holds, for the CSS of the element the builder
// makes in it (Widget::applyCss): once, recorded or not. False outside an As, or once taken
bool takeAs(std::string_view& tag, std::string_view& attrs);

// f() with the CSS scopes of obj's element and of the elements above it open, outermost first,
// as the page had them open when it built obj: a paragraph built again later ({var} changed) is
// styled as at first (#now span). obj not an element's, nor in one: f() as it is
void inScopesOf(lv_obj_t* obj, const std::function<void()>& f);

// While it lives, nothing is recorded: a dry run over content that is built after it
class Pause {
public:
    Pause();
    ~Pause();
    Pause(const Pause&) = delete;
    Pause& operator=(const Pause&) = delete;
};

// ---- What a script does with them ----

Node getElementById(std::string_view id);  // the first one in the page order; 0: none
P::String tagName(Node n);                 // upper case, as in HTML: "LABEL"
P::String textContent(Node n);             // the text it shows, as one string
bool setTextContent(Node n, std::string_view text);  // false: nothing on the screen took it
bool getAttribute(Node n, std::string_view name, P::String& value);  // false: it has none
bool hasAttribute(Node n, std::string_view name);
bool setAttribute(Node n, std::string_view name, std::string_view value);  // false: not an attribute name
void removeAttribute(Node n, std::string_view name);

// classList: the words of class="", CSS applied again after a change. false: a name is empty or
// has a space in it
bool hasClass(Node n, std::string_view name);
bool addClasses(Node n, const P::Array<std::string_view>& names);
bool removeClasses(Node n, const P::Array<std::string_view>& names);
bool toggleClass(Node n, std::string_view name, int force, bool& now);  // force: -1 either way, 0 off, 1 on
P::String className(Node n);

// el.style: the declarations of style="", by their CSS names (background-color, --accent). A
// change rewrites style="" as a browser writes it ("color: red;") and applies CSS again
bool styleProperty(Node n, std::string_view name, P::String& value, bool& important);  // false: none
// value "": taken off. Left alone, as a browser leaves it: a name with a space or a ':', a value
// that is not one declaration ("red; width: 0")
void setStyleProperty(Node n, std::string_view name, std::string_view value, bool important = false);
P::String cssText(Node n);
void setCssText(Node n, std::string_view text);

// ---- The tree: createElement, appendChild, insertBefore, removeChild ----

// An element out of the document. It takes attributes, text and elements; put into the document,
// it is built as the markup would be. 0: not a name a tag can have
Node createElement(std::string_view tag);

enum class TreeChange : uint8_t {
    Done,
    NotAChild,  // ref (or the child to remove) is not a child of the parent
    Loop,       // the child is the parent or above it
    NotABox,    // the parent shows text (<p>, <label>, <button>): TelaOS puts elements into boxes
    Page,       // a page stays where it is
};

// child into parent, before ref (0: the last). An element in the document moves with its objects
// as they are; one taken out comes back so; one made by createElement is built here
TreeChange insertBefore(Node parent, Node child, Node ref);
// child out of the document: its objects wait, hidden, as they are
TreeChange removeChild(Node parent, Node child);
Node parentOf(Node n);  // 0: a page, or the top of what is out of the document
// No script holds n any more: if it is out of the document, its objects go
void release(Node n);

// The elements in the page order, for tools: the simulator's robot writes them out with `tree`.
// The simulator is built against older firmware too (the base of a PR): it looks for UI_DOM_WALK
#define UI_DOM_WALK 1
Node firstChild(Node n);     // n = 0: the first page
Node nextSibling(Node n);    // 0: n is the last one in its parent (a page: the last page)
lv_obj_t* objectOf(Node n);  // its own object; nullptr: it has none (<b> in a paragraph)

// ---- Events (addEventListener) ----

// An event on its way up the elements, as a listener sees it
struct Event {
    std::string_view type;          // "click"
    Node target = 0;                // where it happened
    Node currentTarget = 0;         // whose listeners run now
    bool defaultPrevented = false;  // preventDefault(): what TelaOS does after it is not done
    bool stopped = false;           // stopPropagation(): the elements above do not get it
};

// The script engine runs the listeners e.currentTarget has for e.type
using Dispatch = void (*)(Event& e);
void setDispatch(Dispatch d);

// n is to have listeners for `type`: its object, and those of the elements in it for an event
// that goes up (click), pass it on. false: TelaOS has no such event (click, input, change,
// focus, blur)
bool listen(Node n, std::string_view type);

// What TelaOS does by itself on an LVGL event (it follows a link, opens <details>): not if a
// listener of that event called preventDefault()
bool defaultPrevented(lv_event_t* e);

// The elements selectors match, read as CSS reads them (a state, :checked, is the one the widget
// is in now), in the page order: in the whole app (inside 0) or in `inside`, whose ancestors
// count (.menu a). false: the selectors do not read
bool querySelector(Node inside, std::string_view selectors, Node& found);  // the first; 0: none
bool querySelectorAll(Node inside, std::string_view selectors, P::Array<Node>& found);

}  // namespace Dom
}  // namespace UI
