#include "engines/lua/lua_usb.h"
#include "hal/usbhost.h"
#include "hal/sd_path.h"
#include "utils/log_config.h"

#include <cstdio>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace LuaUsb {

static const char* TAG = "LuaUsb";

// Resolve arg `idx` to an absolute SD path in `buf`. On failure, raises a Lua
// error (invalid path) — callers that prefer nil,err handle mount separately.
static bool resolveArg(lua_State* L, int idx, char* buf, size_t sz) {
    const char* in = luaL_checkstring(L, idx);
    return sdResolvePath(Usb::mountPoint(), in, buf, sz);
}

// Common preamble for every I/O operation: try to mount on demand
// (Usb::ensureMounted re-mounts via the board's Device if the card was
// inserted after boot). If that fails, the card simply isn't there.
static int pushNotMounted(lua_State* L) {
    lua_pushnil(L);
    lua_pushstring(L, "usb: not mounted");
    return 2;
}

// I/O ops fail in one of three ways:
//   1. the card was pulled mid-session (driver wedged, POSIX errors are messy)
//   2. genuine FS error (no such file, ENOSPC, ...)
//   3. driver stuck in a bad state but the card is fine (rare)
// POSIX can't reliably tell these apart. Strategy: on any I/O failure,
// force-unmount so the NEXT sd.* call re-mounts from scratch. That gives a
// one-call recovery after re-inserting the card, without a reboot. For case
// 2 the re-mount succeeds and the original error still surfaces as nil,err.
static int ioFailed(lua_State* L, const char* msg) {
    // Unlike SD, no force-unmount here: USB hot-plug is event-driven — the
    // MSC driver fires DISCONNECTED on unplug and the VFS unregisters itself.
    // A genuine FS error shouldn't tear the whole stack down.
    lua_pushnil(L);
    lua_pushstring(L, msg);
    return 2;
}

static int lua_usb_mounted(lua_State* L) {
    // Probe: reflect the *current* state, not a stale boot-time flag. If the
    // card is absent, ensureMounted fails fast and this stays false.
    Usb::ensureMounted();
    lua_pushboolean(L, Usb::isMounted());
    return 1;
}

static int lua_usb_mount(lua_State* L) {
    // USB-OTG pins are fixed on ESP32-S3 (GPIO19/20): init is uniform.
    lua_pushboolean(L, Usb::init());
    return 1;
}

static int lua_usb_unmount(lua_State* L) {
    lua_pushboolean(L, Usb::deinit());
    return 1;
}

static int lua_usb_info(lua_State* L) {
    if (!Usb::ensureMounted()) { lua_pushnil(L); return 1; }
    uint64_t total = 0, freeB = 0;
    if (!Usb::info(total, freeB)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer) total);
    lua_pushinteger(L, (lua_Integer) freeB);
    return 2;
}

static int lua_usb_exists(lua_State* L) {
    if (!Usb::ensureMounted()) { lua_pushboolean(L, 0); return 1; }
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) { lua_pushboolean(L, 0); return 1; }
    struct stat st;
    lua_pushboolean(L, stat(path, &st) == 0);
    return 1;
}

// sd.isDir(path) -> true|false. False for non-existent paths too, so callers
// can use one check before branching "enter folder vs open file".
static int lua_usb_isDir(lua_State* L) {
    if (!Usb::ensureMounted()) { lua_pushboolean(L, 0); return 1; }
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) { lua_pushboolean(L, 0); return 1; }
    struct stat st;
    lua_pushboolean(L, stat(path, &st) == 0 && S_ISDIR(st.st_mode));
    return 1;
}

// sd.stat(path) -> {size=N, isDir=bool, mtime=N} | nil, err
// mtime is a unix timestamp in seconds (0 if the FS doesn't keep one).
static int lua_usb_stat(lua_State* L) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid path"); return 2;
    }
    struct stat st;
    if (stat(path, &st) != 0) { lua_pushnil(L); lua_pushstring(L, "usb: stat failed"); return 2; }
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer) st.st_size);  lua_setfield(L, -2, "size");
    lua_pushboolean(L, S_ISDIR(st.st_mode));       lua_setfield(L, -2, "isDir");
    lua_pushinteger(L, (lua_Integer) st.st_mtime); lua_setfield(L, -2, "mtime");
    return 1;
}

static int lua_usb_list(lua_State* L) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid path"); return 2;
    }
    DIR* d = opendir(path);
    if (!d) return ioFailed(L, "usb: cannot open dir");
    lua_newtable(L);
    int i = 1;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        lua_pushstring(L, e->d_name);
        lua_rawseti(L, -2, i++);
    }
    closedir(d);
    return 1;
}

static int lua_usb_read(lua_State* L) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid path"); return 2;
    }
    FILE* f = fopen(path, "rb");
    if (!f) return ioFailed(L, "usb: cannot open file");
    std::string data;
    char chunk[1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) data.append(chunk, n);
    fclose(f);
    lua_pushlstring(L, data.data(), data.size());
    return 1;
}

static int write_impl(lua_State* L, const char* mode) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid path"); return 2;
    }
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    FILE* f = fopen(path, mode);
    if (!f) return ioFailed(L, "usb: cannot open for write");
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    if (w != len) return ioFailed(L, "usb: short write");
    lua_pushboolean(L, 1);
    return 1;
}
static int lua_usb_write(lua_State* L)  { return write_impl(L, "wb"); }
static int lua_usb_append(lua_State* L) { return write_impl(L, "ab"); }

static int lua_usb_remove(lua_State* L) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid path"); return 2;
    }
    // FATFS' POSIX shim is picky: remove() on a directory fails with EISDIR,
    // rmdir() on a file with ENOTDIR. Auto-route so callers don't need to know.
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (rmdir(path) != 0) return ioFailed(L, "usb: cannot rmdir (not empty?)");
    } else {
        if (remove(path) != 0) return ioFailed(L, "usb: cannot remove");
    }
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_usb_mkdir(lua_State* L) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid path"); return 2;
    }
    if (mkdir(path, 0775) != 0) return ioFailed(L, "usb: cannot mkdir");
    lua_pushboolean(L, 1);
    return 1;
}

// sd.rename(old, new) -> true | nil, err. Also covers move-within-the-mount.
static int lua_usb_rename(lua_State* L) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char oldp[256], newp[256];
    if (!resolveArg(L, 1, oldp, sizeof(oldp))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid src"); return 2;
    }
    if (!resolveArg(L, 2, newp, sizeof(newp))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid dst"); return 2;
    }
    if (rename(oldp, newp) != 0) return ioFailed(L, "usb: rename failed");
    lua_pushboolean(L, 1);
    return 1;
}

// sd.copy(src, dst) -> true | nil, err. Streams in 4 KB chunks so multi-MB
// files never have to fit in RAM. Removes the partial destination on failure
// so no half-written file masquerades as the real thing.
static int lua_usb_copy(lua_State* L) {
    if (!Usb::ensureMounted()) return pushNotMounted(L);
    char srcp[256], dstp[256];
    if (!resolveArg(L, 1, srcp, sizeof(srcp))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid src"); return 2;
    }
    if (!resolveArg(L, 2, dstp, sizeof(dstp))) {
        lua_pushnil(L); lua_pushstring(L, "usb: invalid dst"); return 2;
    }
    FILE* in = fopen(srcp, "rb");
    if (!in) return ioFailed(L, "usb: cannot open source");
    FILE* out = fopen(dstp, "wb");
    if (!out) { fclose(in); return ioFailed(L, "usb: cannot open destination"); }

    char buf[4096];
    bool ok = true;
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), in);
        if (n == 0) break;
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ok && !feof(in)) ok = false;   // read stopped early without EOF
    fclose(in);
    fclose(out);
    if (!ok) {
        remove(dstp);   // don't leave a half-baked copy behind
        return ioFailed(L, "usb: copy failed");
    }
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg sd_lib[] = {
    {"mounted", lua_usb_mounted},
    {"mount",   lua_usb_mount},
    {"unmount", lua_usb_unmount},
    {"info",    lua_usb_info},
    {"list",    lua_usb_list},
    {"read",    lua_usb_read},
    {"write",   lua_usb_write},
    {"append",  lua_usb_append},
    {"exists",  lua_usb_exists},
    {"isDir",   lua_usb_isDir},
    {"stat",    lua_usb_stat},
    {"remove",  lua_usb_remove},
    {"mkdir",   lua_usb_mkdir},
    {"rename",  lua_usb_rename},
    {"copy",    lua_usb_copy},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    luaL_newlib(L, sd_lib);
    lua_setglobal(L, "usb");
    LOG_I(Log::SD, "Registered: usb.*");
}

} // namespace LuaUsb
