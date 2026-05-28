/**
 * Test: timer.once / timer.interval / timer.clear — new API
 *
 * Tests BOTH callback styles:
 *   timer.once(function() ... end, 100)  -- function reference
 *   timer.once("funcName", 100)          -- string name (via CallQueue)
 *   timer.interval(func, 50)             -- repeating
 *   timer.clear("name")                  -- cancel by name
 *
 * Uses lv_mock_fire_timers() to trigger LVGL timers.
 * Uses CallQueue::process() to drain string-name callbacks.
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"
#include "core/call_queue.h"
#include "console/console.h"

static int g_passed = 0, g_total = 0;
#define TEST(name)     printf("  %-55s ", name); g_total++;
#define PASS()         do { printf("✓\n"); g_passed++; } while(0)
#define FAIL(msg)      printf("✗ %s\n", msg)
#define FAIL_V(f, ...) do { printf("✗ "); printf(f, __VA_ARGS__); printf("\n"); } while(0)
#define SECTION(name)  printf("\n%s:\n", name)

static std::string get(const char* var) {
    return g_core.store().getString(var);
}

static const char* APP_HTML = R"(
<app os="1.0">
  <ui default="/main">
    <page id="main" bgcolor="#000">
      <label align="center" y="10%">{status}</label>
      <label align="center" y="30%">Count: {count}</label>
      <label align="center" y="50%">Tick: {tick}</label>
      <button x="10%" y="70%" w="35%" h="40" onclick="startOnce">Once</button>
      <button x="55%" y="70%" w="35%" h="40" onclick="startInterval">Go</button>
      <button x="10%" y="85%" w="35%" h="40" onclick="startNamedRef">Named</button>
      <button x="55%" y="85%" w="35%" h="40" onclick="doChain">Chain</button>
    </page>
  </ui>

  <state>
    <string name="status" default="ready"/>
    <int name="count" default="0"/>
    <int name="tick" default="0"/>
    <string name="chain" default=""/>
  </state>

  <script language="lua">
    -- timer.once with ANONYMOUS function reference
    function startOnce()
      state.status = "waiting"
      timer.once(function()
        state.count = state.count + 1
        state.status = "fired"
      end, 100)
    end

    -- timer.once with STRING NAME (classic)
    function onTick()
      state.count = state.count + 10
      state.status = "str_fired"
    end
    function startOnceStr()
      state.status = "waiting_str"
      timer.once("onTick", 100)
    end

    -- timer.once with NAMED function reference
    local function doIncrement()
      state.count = state.count + 100
      state.status = "ref_named"
    end
    function startNamedRef()
      timer.once(doIncrement, 50)
    end

    -- timer.interval with anonymous function ref
    function startInterval()
      state.tick = 0
      timer.interval(function()
        state.tick = state.tick + 1
      end, 50)
    end

    -- Chained timers — first fires, schedules second
    function doChain()
      state.chain = ""
      timer.once(function()
        state.chain = state.chain .. "A"
        timer.once(function()
          state.chain = state.chain .. "B"
          timer.once(function()
            state.chain = state.chain .. "C"
            state.status = "chained"
          end, 10)
        end, 10)
      end, 10)
    end

    -- timer.interval with string name + timer.clear
    function intervalTick()
      state.count = state.count + 1000
    end
    function startClearable()
      timer.interval("intervalTick", 50)
    end
    function doClear()
      timer.clear("intervalTick")
    end
  </script>
</app>
)";

static LuaEngine g_lua_engine;

// Fire LVGL timers + drain CallQueue
static void tick() {
    lv_mock_fire_timers();
    CallQueue::process();
}

static void loadApp() {
    LvglMock::reset();
    lv_mock_clear_timers();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_lua_engine.shutdown();
    CallQueue::shutdown();

    // Init CallQueue before engine
    CallQueue::init();

    auto& ui = g_core;
    g_core.initDynamicApp(nullptr);
    ui.render(APP_HTML);

    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_core.store().set(name, def ? def : "");
    }

    g_lua_engine.init();
    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_lua_engine.setState(name, def ? def : "");
    }

    // Wire up CallQueue handler → Lua engine
    CallQueue::setHandler([](const P::String& func) {
        g_lua_engine.call(func.c_str());
    });

    ui.setOnClickHandler([](const char* func) {
        g_lua_engine.call(func);
    });

    const char* code = ui.scriptCode();
    if (code && code[0]) g_lua_engine.execute(code);
}

int main() {
    printf("=== Timer API E2E Tests ===\n");
    loadApp();

    // ─── 1. Initial state ───────────────────────────────

    SECTION("1. Initial state");

    TEST("status = ready") {
        if (get("status") == "ready") PASS();
        else FAIL_V("status='%s'", get("status").c_str());
    }

    TEST("count = 0") {
        if (get("count") == "0") PASS();
        else FAIL_V("count='%s'", get("count").c_str());
    }

    // ─── 2. timer.once with anonymous function ──────────

    SECTION("2. timer.once(function() ... end, ms)");

    TEST("call startOnce → status = waiting") {
        Console::exec("ui call startOnce");
        if (get("status") == "waiting") PASS();
        else FAIL_V("status='%s'", get("status").c_str());
    }

    TEST("count still 0 before timer fires") {
        if (get("count") == "0") PASS();
        else FAIL_V("count='%s'", get("count").c_str());
    }

    TEST("fire → count = 1, status = fired") {
        tick();
        if (get("count") == "1" && get("status") == "fired") PASS();
        else FAIL_V("count='%s' status='%s'", get("count").c_str(), get("status").c_str());
    }

    TEST("fire again → no double-fire (oneshot)") {
        tick();
        if (get("count") == "1") PASS();
        else FAIL_V("count='%s'", get("count").c_str());
    }

    // ─── 3. timer.once with string name ─────────────────

    SECTION("3. timer.once(\"funcName\", ms)");

    TEST("call startOnceStr → status = waiting_str") {
        g_lua_engine.call("startOnceStr");
        if (get("status") == "waiting_str") PASS();
        else FAIL_V("status='%s'", get("status").c_str());
    }

    TEST("fire + process → count = 11, status = str_fired") {
        tick();
        if (get("count") == "11" && get("status") == "str_fired") PASS();
        else FAIL_V("count='%s' status='%s'", get("count").c_str(), get("status").c_str());
    }

    // ─── 4. timer.once with named function ref ──────────

    SECTION("4. timer.once(namedFunc, ms)");

    TEST("call startNamedRef + fire → count = 111") {
        g_lua_engine.call("startNamedRef");
        tick();
        if (get("count") == "111" && get("status") == "ref_named") PASS();
        else FAIL_V("count='%s' status='%s'", get("count").c_str(), get("status").c_str());
    }

    // ─── 5. timer.interval with function ref ────────────

    SECTION("5. timer.interval(function(), ms)");

    TEST("call startInterval → tick = 0") {
        Console::exec("ui call startInterval");
        if (get("tick") == "0") PASS();
        else FAIL_V("tick='%s'", get("tick").c_str());
    }

    TEST("fire → tick = 1") {
        tick();
        if (get("tick") == "1") PASS();
        else FAIL_V("tick='%s'", get("tick").c_str());
    }

    TEST("fire → tick = 2 (repeating)") {
        tick();
        if (get("tick") == "2") PASS();
        else FAIL_V("tick='%s'", get("tick").c_str());
    }

    TEST("fire x3 → tick = 5") {
        tick(); tick(); tick();
        if (get("tick") == "5") PASS();
        else FAIL_V("tick='%s'", get("tick").c_str());
    }

    // ─── 6. Chained timers ──────────────────────────────

    SECTION("6. Chained timers (timer inside timer)");

    TEST("doChain → chain empty") {
        Console::exec("ui call doChain");
        if (get("chain") == "") PASS();
        else FAIL_V("chain='%s'", get("chain").c_str());
    }

    TEST("fire → A") {
        tick();
        if (get("chain") == "A") PASS();
        else FAIL_V("chain='%s'", get("chain").c_str());
    }

    TEST("fire → AB") {
        tick();
        if (get("chain") == "AB") PASS();
        else FAIL_V("chain='%s'", get("chain").c_str());
    }

    TEST("fire → ABC, status = chained") {
        tick();
        if (get("chain") == "ABC" && get("status") == "chained") PASS();
        else FAIL_V("chain='%s' status='%s'", get("chain").c_str(), get("status").c_str());
    }

    // ─── 7. timer.interval string + timer.clear ────────

    SECTION("7. timer.interval(\"name\") + timer.clear");

    TEST("startClearable + fire → count increases") {
        int before = g_core.store().getInt("count");
        g_lua_engine.call("startClearable");
        tick();
        int after = g_core.store().getInt("count");
        if (after == before + 1000) PASS();
        else FAIL_V("before=%d after=%d", before, after);
    }

    TEST("fire again → count +1000 more") {
        int before = g_core.store().getInt("count");
        tick();
        int after = g_core.store().getInt("count");
        if (after == before + 1000) PASS();
        else FAIL_V("before=%d after=%d", before, after);
    }

    TEST("doClear + fire → count stays") {
        g_lua_engine.call("doClear");
        int before = g_core.store().getInt("count");
        tick();
        int after = g_core.store().getInt("count");
        if (after == before) PASS();
        else FAIL_V("before=%d after=%d (not cleared?)", before, after);
    }

    // ═════════════════════════════════════════════════════

    lv_mock_clear_timers();
    CallQueue::shutdown();

    printf("\n────────────────────────────────────────────────────\n");
    if (g_passed == g_total) {
        printf("  ✓ ALL %d TIMER API TESTS PASSED\n", g_total);
    } else {
        printf("  ✗ %d/%d PASSED, %d FAILED\n", g_passed, g_total, g_total - g_passed);
    }
    printf("────────────────────────────────────────────────────\n");
    return g_passed == g_total ? 0 : 1;
}
