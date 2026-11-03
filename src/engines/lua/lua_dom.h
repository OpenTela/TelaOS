#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_dom.h - the app's elements, as a browser gives them to JavaScript (docs/DOM.md)
 *   document.getElementById(id)        -- the element, or nil
 *   el.textContent = "Hi"              -- read and write its text
 *   el:getAttribute(name)              -- string or nil
 *   el:setAttribute(name, value)  el:removeAttribute(name)  el:hasAttribute(name)
 *   el.id  el.tagName
 */

namespace LuaDom {

void registerAll(lua_State* L);

} // namespace LuaDom
