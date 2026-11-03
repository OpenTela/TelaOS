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
// ones (<img> is built as <image>, <input type="range"> as <slider>)
class As {
public:
    As(std::string_view tag, std::string_view attrs);
    ~As();
    As(const As&) = delete;
    As& operator=(const As&) = delete;
};

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

// The elements selectors match, read as CSS reads them (a state, :checked, is the one the widget
// is in now), in the page order: in the whole app (inside 0) or in `inside`, whose ancestors
// count (.menu a). false: the selectors do not read
bool querySelector(Node inside, std::string_view selectors, Node& found);  // the first; 0: none
bool querySelectorAll(Node inside, std::string_view selectors, P::Array<Node>& found);

}  // namespace Dom
}  // namespace UI
