#pragma once
/**
 * dom.h — the elements of an app as its script sees them, as a browser's DOM (docs/DOM.md).
 *
 * The page walkers and the widget builders make LVGL objects straight from the markup; the tree
 * of elements is recorded on the way. An element is entered and left while its content is built
 * (UI::CssScope), and it gets its object when it is styled (Widget::applyCss). It keeps its
 * attributes as they are written: reading one decodes it, writing one also does on the screen
 * what the attribute does in the markup (hidden, disabled, checked, value...).
 *
 * An element is a handle. A new build (the next app) makes new handles: the old ones find nothing.
 */

#include "lvgl.h"
#include "utils/psram_alloc.h"
#include <cstdint>
#include <string_view>

namespace UI {
namespace Dom {

using Node = uint32_t;  // an element; 0: none

// ---- Recording: the build of an app's pages calls these ----

void begin();  // a build starts: the elements of the one before are forgotten
void end();    // ...and is over: what is built later (a list's rows) is not an element
void clear();  // the app is gone: its elements too

bool enter(std::string_view tag, std::string_view attrs);  // an element's content starts (false: not recorded)
void leave();                                              // ...and ends (after an enter that was)
// An element got its object. The same attributes (the same place in the text) tell that it is
// the element just entered or left, not a new one
void element(lv_obj_t* obj, std::string_view tag, std::string_view attrs);

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

}  // namespace Dom
}  // namespace UI
