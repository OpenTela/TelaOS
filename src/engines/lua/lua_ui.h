#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_ui.h - ui.* namespace
 *   ui.navigate(page)    + alias navigate()
 *   ui.setAttr(id, attr, val)
 *   ui.getAttr(id, attr)
 *   ui.focus(id)
 *   ui.freeze() / ui.unfreeze()
 */

namespace LuaUI {

void registerAll(lua_State* L);

} // namespace LuaUI
