#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_usb.h — exposes a connected USB flash drive to Lua as the `usb.*`
 * namespace, with the exact shape of sd.* — code that talks to the SD card
 * already knows how to talk to a flash drive.
 *
 *   usb.mounted()           -> bool   (true once enumeration completes)
 *   usb.mount()             -> bool   (idempotent USB Host stack init)
 *   usb.unmount()           -> bool   (full stack teardown)
 *   usb.info()              -> total, free (bytes) | nil
 *   usb.list/read/write/append/exists/isDir/stat/remove/mkdir/rename/copy
 *                            — same contracts as sd.*
 *
 * Paths are relative to "/usb"; a leading "/" or explicit "/usb/..." both
 * accepted, ".." rejected. Note: unlike sd.*, hot-plug here is event-driven
 * (the MSC driver announces connect/disconnect), so `usb.mounted()` flips on
 * its own shortly after plugging a drive in.
 */
namespace LuaUsb {

void registerAll(lua_State* L);

} // namespace LuaUsb
