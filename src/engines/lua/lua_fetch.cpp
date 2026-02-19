#include "engines/lua/lua_fetch.h"
#include "utils/log_config.h"
#include "ble/ble_bridge.h"




namespace LuaFetch {

static const char* TAG = "LuaFetch";
static const char* VERSION = "3.0.0";

static lua_State* g_luaState = nullptr;

// Helper: push ArduinoJson value to Lua stack
static void pushJsonToLua(lua_State* L, JsonVariantConst v) {
    if (v.isNull()) {
        lua_pushnil(L);
    } else if (v.is<bool>()) {
        lua_pushboolean(L, v.as<bool>());
    } else if (v.is<int>() || v.is<long>()) {
        lua_pushinteger(L, v.as<long>());
    } else if (v.is<float>() || v.is<double>()) {
        lua_pushnumber(L, v.as<double>());
    } else if (v.is<const char*>()) {
        lua_pushstring(L, v.as<const char*>());
    } else if (v.is<JsonObjectConst>()) {
        lua_newtable(L);
        JsonObjectConst obj = v.as<JsonObjectConst>();
        for (JsonPairConst kv : obj) {
            lua_pushstring(L, kv.key().c_str());
            pushJsonToLua(L, kv.value());
            lua_settable(L, -3);
        }
    } else if (v.is<JsonArrayConst>()) {
        lua_newtable(L);
        JsonArrayConst arr = v.as<JsonArrayConst>();
        int idx = 1;
        for (JsonVariantConst item : arr) {
            lua_pushinteger(L, idx++);
            pushJsonToLua(L, item);
            lua_settable(L, -3);
        }
    } else {
        lua_pushstring(L, v.as<std::string>().c_str());
    }
}

static int lua_fetch(lua_State* L) {
    const char* method = "GET";
    const char* url = nullptr;
    const char* body = nullptr;
    const char* format = nullptr;
    bool authorize = false;
    P::Array<P::String> fields;
    int callbackRef = LUA_NOREF;
    
    if (lua_isstring(L, 1)) {
        url = lua_tostring(L, 1);
        if (lua_isfunction(L, 2)) {
            lua_pushvalue(L, 2);
            callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    } else if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "method");
        if (lua_isstring(L, -1)) method = lua_tostring(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "url");
        if (lua_isstring(L, -1)) url = lua_tostring(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "body");
        if (lua_isstring(L, -1)) body = lua_tostring(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "authorize");
        if (lua_isboolean(L, -1)) authorize = lua_toboolean(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "format");
        if (lua_isstring(L, -1)) format = lua_tostring(L, -1);
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "fields");
        if (lua_istable(L, -1)) {
            int len = lua_rawlen(L, -1);
            for (int i = 1; i <= len; i++) {
                lua_rawgeti(L, -1, i);
                if (lua_isstring(L, -1)) {
                    fields.push_back(lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        
        if (lua_isfunction(L, 2)) {
            lua_pushvalue(L, 2);
            callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    }
    
    if (!url) {
        LOG_E(Log::LUA, "fetch: missing URL");
        return 0;
    }
    
    if (!BLEBridge::isConnected()) {
        LOG_W(Log::LUA, "fetch: BLE not connected");
        if (callbackRef != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, callbackRef);
            lua_newtable(L);
            lua_pushinteger(L, 0);
            lua_setfield(L, -2, "status");
            lua_pushstring(L, "BLE not connected");
            lua_setfield(L, -2, "error");
            lua_pushboolean(L, false);
            lua_setfield(L, -2, "ok");
            lua_call(L, 1, 0);
            luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);
        }
        return 0;
    }
    
    LOG_D(Log::LUA, "fetch %s %s", method, url);
    
    int capturedRef = callbackRef;
    lua_State* capturedL = L;
    bool parseAsJson = (format && strcmp(format, "json") == 0);
    
    BLEBridge::sendFetchRequest(method, url, body, authorize, format, fields,
        [capturedRef, capturedL, parseAsJson](int status, const P::String& respBody) {
            if (capturedRef == LUA_NOREF) return;
            
            LOG_D(Log::LUA, "fetch callback: status=%d body=%d bytes", status, (int)respBody.length());
            
            lua_rawgeti(capturedL, LUA_REGISTRYINDEX, capturedRef);
            lua_newtable(capturedL);
            
            lua_pushinteger(capturedL, status);
            lua_setfield(capturedL, -2, "status");
            
            if (parseAsJson && !respBody.empty()) {
                auto doc = PsramJsonDoc();
                DeserializationError err = deserializeJson(doc, respBody);
                if (err) {
                    lua_pushstring(capturedL, respBody.c_str());
                } else {
                    pushJsonToLua(capturedL, doc.as<JsonVariantConst>());
                }
            } else {
                lua_pushstring(capturedL, respBody.c_str());
            }
            lua_setfield(capturedL, -2, "body");
            
            lua_pushboolean(capturedL, status >= 200 && status < 300);
            lua_setfield(capturedL, -2, "ok");
            
            if (lua_pcall(capturedL, 1, 0, 0) != LUA_OK) {
                LOG_E(Log::LUA, "fetch callback error: %s", lua_tostring(capturedL, -1));
                lua_pop(capturedL, 1);
            }
            
            luaL_unref(capturedL, LUA_REGISTRYINDEX, capturedRef);
        }
    );
    
    return 0;
}

// net.connected()
static int lua_net_connected(lua_State* L) {
    lua_pushboolean(L, BLEBridge::isConnected());
    return 1;
}

static const luaL_Reg net_lib[] = {
    {"fetch",     lua_fetch},
    {"connected", lua_net_connected},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    g_luaState = L;
    
    // net.* namespace
    luaL_newlib(L, net_lib);
    lua_setglobal(L, "net");
    
    // Alias: fetch()
    lua_register(L, "fetch", lua_fetch);
    
    LOG_I(Log::LUA, "LuaFetch v%s registered: net.* + fetch()", VERSION);
}

} // namespace LuaFetch
