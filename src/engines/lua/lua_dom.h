#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_dom.h - the app's elements, as a browser gives them to JavaScript (docs/DOM.md)
 *   document.getElementById(id)        -- the element, or nil
 *   document:querySelector(s)  document:querySelectorAll(s)  el:querySelector(s)...
 *   el.textContent = "Hi"              -- read and write its text
 *   el:getAttribute(name)              -- string or nil
 *   el:setAttribute(name, value)  el:removeAttribute(name)  el:hasAttribute(name)
 *   el.id  el.tagName  el.className  el.classList:add(name)...  el.style.color = "red"
 *   el:addEventListener("click", function(e) ... end)  el:removeEventListener(type, fn)
 */

namespace LuaDom {

void registerAll(lua_State* L);
void shutdown(lua_State* L);  // before L is closed: no event goes to its listeners

} // namespace LuaDom
