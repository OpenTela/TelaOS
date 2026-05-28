#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_sd.h — exposes the SD card to Lua as the `sd.*` namespace.
 *
 *   sd.mounted()            -> bool
 *   sd.mount()              -> bool        (re-mount, e.g. after hot-plug)
 *   sd.unmount()            -> bool
 *   sd.info()               -> total, free (bytes) | nil
 *   sd.list(path)           -> { names... } | nil, err
 *   sd.read(path)           -> data | nil, err
 *   sd.write(path, data)    -> true | nil, err
 *   sd.append(path, data)   -> true | nil, err
 *   sd.exists(path)         -> bool
 *   sd.remove(path)         -> true | nil, err
 *   sd.mkdir(path)          -> true | nil, err
 *
 * Paths are relative to the SD mount ("/sd"); a leading "/" or an explicit
 * "/sd/..." prefix are both accepted. ".." segments are rejected.
 */
namespace LuaSd {

void registerAll(lua_State* L);

} // namespace LuaSd
