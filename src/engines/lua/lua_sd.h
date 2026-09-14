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
 *   sd.isDir(path)          -> bool
 *   sd.stat(path)           -> {size, isDir, mtime} | nil, err
 *   sd.remove(path)         -> true | nil, err   (files and empty dirs)
 *   sd.mkdir(path)          -> true | nil, err
 *   sd.rename(old, new)     -> true | nil, err
 *   sd.copy(src, dst)       -> true | nil, err   (streamed, 4 KB chunks)
 *
 * Hot-plug: every operation starts with Sd::ensureMounted(), so a card
 * inserted after boot mounts on the next call. Any I/O failure force-unmounts
 * the driver, so a pulled-and-reinserted card recovers on the following call
 * — no reboot needed.
 *
 * Paths are relative to the SD mount ("/sd"); a leading "/" or an explicit
 * "/sd/..." prefix are both accepted. ".." segments are rejected.
 */
namespace LuaSd {

void registerAll(lua_State* L);

} // namespace LuaSd
