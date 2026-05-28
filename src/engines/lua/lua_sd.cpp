#include "engines/lua/lua_sd.h"
#include "hal/sdcard.h"
#include "hal/sd_path.h"
#include "hal/device.h"
#include "utils/log_config.h"

#include <cstdio>
#include <string>
#include <dirent.h>
#include <sys/stat.h>

namespace LuaSd {

static const char* TAG = "LuaSd";

// Resolve arg `idx` to an absolute SD path in `buf`. On failure, raises a Lua
// error (invalid path) — callers that prefer nil,err handle mount separately.
static bool resolveArg(lua_State* L, int idx, char* buf, size_t sz) {
    const char* in = luaL_checkstring(L, idx);
    return sdResolvePath(Sd::mountPoint(), in, buf, sz);
}

static int pushErrno(lua_State* L) {
    lua_pushnil(L);
    lua_pushstring(L, "sd: I/O error");
    return 2;
}

static int lua_sd_mounted(lua_State* L) {
    lua_pushboolean(L, Sd::isMounted());
    return 1;
}

static int lua_sd_mount(lua_State* L) {
    // Pins are board-specific; the board's Device override knows them.
    lua_pushboolean(L, Device::inst().mountSdCard());
    return 1;
}

static int lua_sd_unmount(lua_State* L) {
    lua_pushboolean(L, Sd::unmount());
    return 1;
}

static int lua_sd_info(lua_State* L) {
    uint64_t total = 0, freeB = 0;
    if (!Sd::info(total, freeB)) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer) total);
    lua_pushinteger(L, (lua_Integer) freeB);
    return 2;
}

static int lua_sd_exists(lua_State* L) {
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) { lua_pushboolean(L, 0); return 1; }
    struct stat st;
    lua_pushboolean(L, stat(path, &st) == 0);
    return 1;
}

static int lua_sd_list(lua_State* L) {
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "sd: invalid path"); return 2;
    }
    DIR* d = opendir(path);
    if (!d) { lua_pushnil(L); lua_pushstring(L, "sd: cannot open dir"); return 2; }
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

static int lua_sd_read(lua_State* L) {
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "sd: invalid path"); return 2;
    }
    FILE* f = fopen(path, "rb");
    if (!f) { lua_pushnil(L); lua_pushstring(L, "sd: cannot open file"); return 2; }
    std::string data;
    char chunk[1024];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) data.append(chunk, n);
    fclose(f);
    lua_pushlstring(L, data.data(), data.size());
    return 1;
}

static int write_impl(lua_State* L, const char* mode) {
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "sd: invalid path"); return 2;
    }
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    FILE* f = fopen(path, mode);
    if (!f) { lua_pushnil(L); lua_pushstring(L, "sd: cannot open for write"); return 2; }
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    if (w != len) return pushErrno(L);
    lua_pushboolean(L, 1);
    return 1;
}
static int lua_sd_write(lua_State* L)  { return write_impl(L, "wb"); }
static int lua_sd_append(lua_State* L) { return write_impl(L, "ab"); }

static int lua_sd_remove(lua_State* L) {
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "sd: invalid path"); return 2;
    }
    if (remove(path) != 0) return pushErrno(L);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_sd_mkdir(lua_State* L) {
    char path[256];
    if (!resolveArg(L, 1, path, sizeof(path))) {
        lua_pushnil(L); lua_pushstring(L, "sd: invalid path"); return 2;
    }
    if (mkdir(path, 0775) != 0) return pushErrno(L);
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg sd_lib[] = {
    {"mounted", lua_sd_mounted},
    {"mount",   lua_sd_mount},
    {"unmount", lua_sd_unmount},
    {"info",    lua_sd_info},
    {"list",    lua_sd_list},
    {"read",    lua_sd_read},
    {"write",   lua_sd_write},
    {"append",  lua_sd_append},
    {"exists",  lua_sd_exists},
    {"remove",  lua_sd_remove},
    {"mkdir",   lua_sd_mkdir},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    luaL_newlib(L, sd_lib);
    lua_setglobal(L, "sd");
    LOG_I(Log::SD, "Registered: sd.*");
}

} // namespace LuaSd
