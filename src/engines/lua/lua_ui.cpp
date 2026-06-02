#include "engines/lua/lua_ui.h"
#include "core/core.h"
#include "utils/log_config.h"
#include "utils/psram_alloc.h"
#include "core/core.h"
#include <lvgl.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

// Forward declarations - implemented in core.cpp
namespace UI {
    bool focusInput(const char* id);
    bool setWidgetAttr(const char* id, const char* attr, const char* value);
    P::String getWidgetAttr(const char* id, const char* attr);
}

// Defined in ui_widget_builder.cpp (global namespace).
uint32_t parse_color(const char* s);

namespace LuaUI {

static const char* TAG = "LuaUI";

static int lua_navigate(lua_State* L) {
    const char* page = luaL_checkstring(L, 1);
    g_core.showPage(page);
    return 0;
}

static int lua_focus(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    bool ok = UI::focusInput(id);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int lua_setAttr(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* attr = luaL_checkstring(L, 2);
    const char* value = luaL_checkstring(L, 3);
    bool ok = UI::setWidgetAttr(id, attr, value);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int lua_getAttr(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    const char* attr = luaL_checkstring(L, 2);
    P::String val = UI::getWidgetAttr(id, attr);
    lua_pushstring(L, val.c_str());
    return 1;
}

static int lua_freeze(lua_State* L) {
    g_core.freeze();
    return 0;
}

static int lua_unfreeze(lua_State* L) {
    g_core.unfreeze();
    return 0;
}

// ============ ui.msgbox ============
//
//   ui.msgbox({ title="...", text="...", buttons={"Cancel","OK"} }, cb)
//
// Modal popup. Tapping a footer button or the corner ✕ closes the box and
// invokes cb(index, label). Index is 1-based for buttons; tapping ✕ or
// outside the box sends index=0 with label="".
//
// Only one msgbox is active at a time; opening a new one while another is
// up closes the previous without firing its callback.

struct MsgBoxState {
    int            cbRef    = LUA_NOREF;  // Lua function ref
    lua_State*     L        = nullptr;
    lv_obj_t*      mbox     = nullptr;
    lv_obj_t*      backdrop = nullptr;
    bool           closing  = false;      // re-entry guard
};

static MsgBoxState g_mbox;

// Internal helper: fire callback (if any), clear state. Object deletion is
// handled by the caller — either LVGL itself (close-button) or our own
// dismiss() via lv_obj_delete_async() (footer buttons / backdrop tap).
static void msgbox_fire_callback(int index, const char* label) {
    if (g_mbox.closing) return;
    g_mbox.closing = true;

    int cbRef = g_mbox.cbRef;
    lua_State* L = g_mbox.L;
    g_mbox.cbRef    = LUA_NOREF;
    g_mbox.L        = nullptr;
    g_mbox.mbox     = nullptr;
    g_mbox.backdrop = nullptr;

    if (cbRef != LUA_NOREF && L) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cbRef);
        if (lua_isfunction(L, -1)) {
            lua_pushinteger(L, index);
            lua_pushstring(L, label ? label : "");
            if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
                LOG_E(Log::LUA, "msgbox callback error: %s", lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } else {
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, cbRef);
    }
}

// Async-delete is critical: we can't delete LVGL objects from inside their
// own event handler — LVGL still walks the object after the callback returns.
static void msgbox_dismiss(int index, const char* label) {
    lv_obj_t* mbox     = g_mbox.mbox;
    lv_obj_t* backdrop = g_mbox.backdrop;
    msgbox_fire_callback(index, label);
    if (backdrop) lv_obj_delete_async(backdrop);
    if (mbox)     lv_obj_delete_async(mbox);
}

static void msgbox_button_event(lv_event_t* e) {
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
    const char* label = (lbl) ? lv_label_get_text(lbl) : "";
    msgbox_dismiss(index, label);
}

static void msgbox_backdrop_event(lv_event_t*) {
    msgbox_dismiss(0, "");
}

// The built-in ✕ button calls lv_msgbox_close() which deletes the mbox
// itself. We just need to react to "mbox is going away" — listen for
// LV_EVENT_DELETE on the mbox, fire our callback, and clean up the
// backdrop without double-deleting the mbox.
static void msgbox_delete_event(lv_event_t*) {
    if (g_mbox.closing) return;          // we initiated this delete; nothing to do
    lv_obj_t* backdrop = g_mbox.backdrop;
    msgbox_fire_callback(0, "");
    if (backdrop) lv_obj_delete_async(backdrop);
}

static int lua_msgbox(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    // optional callback at arg 2
    int cbRef = LUA_NOREF;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
        luaL_checktype(L, 2, LUA_TFUNCTION);
        lua_pushvalue(L, 2);
        cbRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    // Pull title / text
    lua_getfield(L, 1, "title");
    const char* title = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);

    lua_getfield(L, 1, "text");
    const char* text = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);

    // Dismiss any existing box silently (no callback fired)
    if (g_mbox.mbox || g_mbox.backdrop) {
        if (g_mbox.cbRef != LUA_NOREF) {
            luaL_unref(g_mbox.L, LUA_REGISTRYINDEX, g_mbox.cbRef);
            g_mbox.cbRef = LUA_NOREF;
        }
        if (g_mbox.mbox)     { lv_obj_delete(g_mbox.mbox);     g_mbox.mbox = nullptr; }
        if (g_mbox.backdrop) { lv_obj_delete(g_mbox.backdrop); g_mbox.backdrop = nullptr; }
    }

    lv_obj_t* screen = lv_screen_active();

    // Full-screen translucent backdrop; tapping it cancels.
    lv_obj_t* bd = lv_obj_create(screen);
    lv_obj_remove_style_all(bd);
    lv_obj_set_size(bd, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bd, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bd, LV_OPA_50, LV_PART_MAIN);
    lv_obj_add_flag(bd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bd, msgbox_backdrop_event, LV_EVENT_CLICKED, nullptr);

    // The msgbox itself
    lv_obj_t* mb = lv_msgbox_create(screen);
    if (title && *title) lv_msgbox_add_title(mb, title);
    lv_msgbox_add_close_button(mb);                  // the corner ✕ (LVGL closes mb itself)
    if (text && *text)   lv_msgbox_add_text(mb, text);

    // Custom button colours (optional): colors={"#3a7afe","#7a1f1f"} aligned to buttons[]
    P::Array<uint32_t> btnColors;
    lua_getfield(L, 1, "colors");
    if (lua_istable(L, -1)) {
        int n = (int)lua_rawlen(L, -1);
        btnColors.reserve(n);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            uint32_t c = lua_isstring(L, -1) ? parse_color(lua_tostring(L, -1)) : 0;
            btnColors.push_back(c);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    // Footer buttons from buttons={"a","b",...}
    lua_getfield(L, 1, "buttons");
    if (lua_istable(L, -1)) {
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            const char* lbl = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);
            lv_obj_t* btn = lv_msgbox_add_footer_button(mb, lbl);
            lv_obj_add_event_cb(btn, msgbox_button_event, LV_EVENT_CLICKED,
                                (void*)(intptr_t)i);
            if (i - 1 < (int)btnColors.size() && btnColors[i - 1] != 0) {
                lv_obj_set_style_bg_color(btn, lv_color_hex(btnColors[i - 1]), LV_PART_MAIN);
            }
        }
    }
    lua_pop(L, 1);

    // React to LVGL closing the mbox (e.g. via the corner ✕) without
    // double-deleting from our side.
    lv_obj_add_event_cb(mb, msgbox_delete_event, LV_EVENT_DELETE, nullptr);

    g_mbox.cbRef    = cbRef;
    g_mbox.L        = L;
    g_mbox.mbox     = mb;
    g_mbox.backdrop = bd;
    return 0;
}

// ui.confirm(text, onYes [, onNo])  — convenience wrapper over msgbox.
// Layout matches the usual mobile convention: "Yes" on the left as the
// affirmative/destructive action, "No" on the right as the safe default.
// Colours: Yes = red (#7a1f1f), No = neutral grey (#26262e).
static int lua_confirm(lua_State* L) {
    const char* text = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    bool hasNo = (lua_gettop(L) >= 3 && lua_isfunction(L, 3));

    // Build args table { title=, text=, buttons={"Yes","No"}, colors={…} }
    lua_newtable(L);
    lua_pushstring(L, "Confirm"); lua_setfield(L, -2, "title");
    lua_pushstring(L, text);      lua_setfield(L, -2, "text");

    lua_newtable(L);
    lua_pushstring(L, "Yes"); lua_rawseti(L, -2, 1);
    lua_pushstring(L, "No");  lua_rawseti(L, -2, 2);
    lua_setfield(L, -2, "buttons");

    lua_newtable(L);
    lua_pushstring(L, "#7a1f1f"); lua_rawseti(L, -2, 1);   // Yes — destructive red
    lua_pushstring(L, "#26262e"); lua_rawseti(L, -2, 2);   // No  — neutral
    lua_setfield(L, -2, "colors");

    // Dispatching closure: idx 1 = Yes, idx 2 = No, idx 0 = cancel (X / tap-out)
    // Cancel falls through the same branch as No.
    lua_pushvalue(L, 2);                          // upvalue 1 = onYes
    if (hasNo) lua_pushvalue(L, 3); else lua_pushnil(L);  // upvalue 2 = onNo or nil
    lua_pushcclosure(L, [](lua_State* L2) -> int {
        int idx = (int)luaL_checkinteger(L2, 1);
        if (idx == 1) {
            lua_pushvalue(L2, lua_upvalueindex(1));
        } else {
            // No, X, or backdrop -> onNo (if provided)
            lua_pushvalue(L2, lua_upvalueindex(2));
            if (lua_isnil(L2, -1)) { lua_pop(L2, 1); return 0; }
        }
        if (lua_pcall(L2, 0, 0, 0) != LUA_OK) {
            LOG_E(Log::LUA, "confirm callback error: %s", lua_tostring(L2, -1));
            lua_pop(L2, 1);
        }
        return 0;
    }, 2);

    // Reorder stack to be the args of lua_msgbox: argsTable, closure
    lua_remove(L, 1);                              // drop original text
    lua_remove(L, 1);                              // drop onYes
    if (hasNo) lua_remove(L, 1);                   // drop onNo
    return lua_msgbox(L);
}


static const luaL_Reg ui_lib[] = {
    {"navigate", lua_navigate},
    {"setAttr",  lua_setAttr},
    {"getAttr",  lua_getAttr},
    {"focus",    lua_focus},
    {"freeze",   lua_freeze},
    {"unfreeze", lua_unfreeze},
    {"msgbox",   lua_msgbox},
    {"confirm",  lua_confirm},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    // ui.* namespace
    luaL_newlib(L, ui_lib);
    lua_setglobal(L, "ui");
    
    // Aliases for backward compat
    lua_register(L, "navigate", lua_navigate);
    lua_register(L, "focus", lua_focus);
    lua_register(L, "setAttr", lua_setAttr);
    lua_register(L, "getAttr", lua_getAttr);
    
    LOG_I(Log::LUA, "Registered: ui.* + navigate/focus/setAttr/getAttr()");
}

} // namespace LuaUI
