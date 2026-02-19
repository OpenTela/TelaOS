/**
 * Test: Declarative Timers
 * 
 * Tests the full pipeline:
 *   <timer interval="1000" call="tick"/> in HTML
 *   → UI::Engine parses timer declarations
 *   → Lua function updates state
 *   → State::store() reflects changes
 *
 * No FreeRTOS needed: we simulate ticks by calling Lua functions directly.
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)
#define FAIL_V(fmt, ...) do { printf("✗ "); printf(fmt, __VA_ARGS__); printf("\n"); } while(0)

// ── Helper: parse HTML, extract timers/state/script, init Lua ──

struct App {
    LuaEngine engine;
    
    bool load(const char* html) {
        LvglMock::reset();
        LvglMock::create_screen(480, 480);
        
        engine.shutdown();
        
        UI::Engine::instance().init();
        UI::Engine::instance().render(html);
        auto& ui = UI::Engine::instance();
        
        // Load state defaults (same as ScriptManager::loadState)
        for (int i = 0; i < ui.stateCount(); i++) {
            const char* name = ui.stateVarName(i);
            const char* vtype = ui.stateVarType(i);
            const char* def = ui.stateVarDefault(i);
            if (!name) continue;
            
            if (strcmp(vtype, "int") == 0)
                State::store().set(name, def ? def : "0");
            else if (strcmp(vtype, "bool") == 0)
                State::store().set(name, def ? def : "false");
            else
                State::store().set(name, def ? def : "");
        }
        
        // Init Lua and sync state
        engine.init();
        for (int i = 0; i < ui.stateCount(); i++) {
            const char* name = ui.stateVarName(i);
            const char* vtype = ui.stateVarType(i);
            const char* def = ui.stateVarDefault(i);
            if (!name) continue;
            
            if (strcmp(vtype, "int") == 0)
                engine.setState(name, def ? atoi(def) : 0);
            else if (strcmp(vtype, "bool") == 0)
                engine.setState(name, def && strcmp(def, "true") == 0);
            else
                engine.setState(name, def ? def : "");
        }
        
        // Execute script
        const char* code = ui.scriptCode();
        if (code && code[0]) {
            return engine.execute(code);
        }
        return true;
    }
    
    // Simulate one timer tick
    bool tick(const char* funcName) {
        return engine.call(funcName);
    }
    
    std::string state(const char* key) {
        return State::store().getString(key);
    }
    
    ~App() { engine.shutdown(); }
};

// ═══════════════════════════════════════════════════════
//  HTML apps for testing
// ═══════════════════════════════════════════════════════

const char* COUNTER_APP = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="time" align="center" y="10%">{time}</label>
    </page>
  </ui>
  <state>
    <string name="time" default="00:00"/>
    <int name="seconds" default="0"/>
  </state>
  <timer interval="1000" call="tick"/>
  <script language="lua">
    function tick()
      state.seconds = state.seconds + 1
      local m = math.floor(state.seconds / 60) % 60
      local s = state.seconds % 60
      state.time = string.format("%02d:%02d", m, s)
    end
  </script>
</app>
)";

const char* MULTI_TIMER_APP = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="fast">{fast}</label>
      <label id="slow">{slow}</label>
    </page>
  </ui>
  <state>
    <int name="fast" default="0"/>
    <int name="slow" default="0"/>
  </state>
  <timer interval="100" call="tickFast"/>
  <timer interval="1000" call="tickSlow"/>
  <script language="lua">
    function tickFast()
      state.fast = state.fast + 1
    end
    function tickSlow()
      state.slow = state.slow + 10
    end
  </script>
</app>
)";

const char* TOGGLE_APP = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="blink" visible="{blinkOn}">*</label>
    </page>
  </ui>
  <state>
    <string name="blinkOn" default="true"/>
  </state>
  <timer interval="500" call="blink"/>
  <script language="lua">
    function blink()
      if state.blinkOn == "true" then
        state.blinkOn = "false"
      else
        state.blinkOn = "true"
      end
    end
  </script>
</app>
)";

const char* WEATHER_STYLE_APP = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="temp">{temperature}°C</label>
      <label id="status">{status}</label>
    </page>
  </ui>
  <state>
    <string name="temperature" default="--"/>
    <string name="status" default="Loading..."/>
    <int name="updateCount" default="0"/>
  </state>
  <timer interval="30000" call="updateWeather"/>
  <script language="lua">
    local cities = {"Moscow", "London", "Tokyo"}
    local temps  = {"22", "-1", "31"}

    function updateWeather()
      state.updateCount = state.updateCount + 1
      local idx = ((state.updateCount - 1) % #cities) + 1
      state.temperature = temps[idx]
      state.status = cities[idx]
    end
  </script>
</app>
)";

const char* NO_TIMER_APP = R"(
<app>
  <ui default="/main">
    <page id="main">
      <label id="msg">{msg}</label>
    </page>
  </ui>
  <state>
    <string name="msg" default="static"/>
  </state>
  <script language="lua">
    function manual()
      state.msg = "updated"
    end
  </script>
</app>
)";

// ═══════════════════════════════════════════════════════
//  Tests
// ═══════════════════════════════════════════════════════

int main() {
    printf("=== Declarative Timer Tests ===\n\n");
    int passed = 0, total = 0;
    App app;

    // ── 1. Timer parsing ──
    printf("HTML timer parsing:\n");

    TEST("single timer parsed") {
        app.load(COUNTER_APP);
        auto& ui = UI::Engine::instance();
        if (ui.timerCount() == 1) PASS();
        else FAIL_V("count=%d", ui.timerCount());
    }

    TEST("timer interval correct") {
        auto& ui = UI::Engine::instance();
        if (ui.timerInterval(0) == 1000) PASS();
        else FAIL_V("interval=%d", ui.timerInterval(0));
    }

    TEST("timer callback correct") {
        auto& ui = UI::Engine::instance();
        if (ui.timerCallback(0) && strcmp(ui.timerCallback(0), "tick") == 0) PASS();
        else FAIL_V("callback='%s'", ui.timerCallback(0) ? ui.timerCallback(0) : "NULL");
    }

    TEST("multiple timers parsed") {
        app.load(MULTI_TIMER_APP);
        auto& ui = UI::Engine::instance();
        if (ui.timerCount() == 2) PASS();
        else FAIL_V("count=%d", ui.timerCount());
    }

    TEST("no timer = count 0") {
        app.load(NO_TIMER_APP);
        if (UI::Engine::instance().timerCount() == 0) PASS();
        else FAIL("expected 0");
    }

    // ── 2. Counter timer ──
    printf("\ncounter timer (tick → seconds → formatted time):\n");

    TEST("initial state = 00:00") {
        app.load(COUNTER_APP);
        if (app.state("time") == "00:00" && app.state("seconds") == "0") PASS();
        else FAIL_V("time='%s' sec='%s'", app.state("time").c_str(), app.state("seconds").c_str());
    }

    TEST("after 1 tick = 00:01") {
        app.tick("tick");
        if (app.state("time") == "00:01") PASS();
        else FAIL_V("got '%s'", app.state("time").c_str());
    }

    TEST("after 59 more ticks = 01:00") {
        for (int i = 0; i < 59; i++) app.tick("tick");
        if (app.state("time") == "01:00" && app.state("seconds") == "60") PASS();
        else FAIL_V("time='%s' sec='%s'", app.state("time").c_str(), app.state("seconds").c_str());
    }

    TEST("after 60 more = 02:00") {
        for (int i = 0; i < 60; i++) app.tick("tick");
        if (app.state("time") == "02:00") PASS();
        else FAIL_V("got '%s'", app.state("time").c_str());
    }

    // ── 3. Multiple timers ──
    printf("\nmultiple timers (fast + slow):\n");

    TEST("initial: both 0") {
        app.load(MULTI_TIMER_APP);
        if (app.state("fast") == "0" && app.state("slow") == "0") PASS();
        else FAIL("not zero");
    }

    TEST("5 fast ticks, 1 slow tick") {
        for (int i = 0; i < 5; i++) app.tick("tickFast");
        app.tick("tickSlow");
        bool ok = app.state("fast") == "5" && app.state("slow") == "10";
        if (ok) PASS();
        else FAIL_V("fast='%s' slow='%s'", app.state("fast").c_str(), app.state("slow").c_str());
    }

    TEST("10:1 ratio simulation") {
        // 10 more fast, 1 more slow
        for (int i = 0; i < 10; i++) app.tick("tickFast");
        app.tick("tickSlow");
        bool ok = app.state("fast") == "15" && app.state("slow") == "20";
        if (ok) PASS();
        else FAIL_V("fast='%s' slow='%s'", app.state("fast").c_str(), app.state("slow").c_str());
    }

    // ── 4. Toggle/blink timer ──
    printf("\ntoggle timer (boolean flip):\n");

    TEST("initial: visible") {
        app.load(TOGGLE_APP);
        if (app.state("blinkOn") == "true") PASS();
        else FAIL_V("got '%s'", app.state("blinkOn").c_str());
    }

    TEST("tick 1: hidden") {
        app.tick("blink");
        if (app.state("blinkOn") == "false") PASS();
        else FAIL_V("got '%s'", app.state("blinkOn").c_str());
    }

    TEST("tick 2: visible again") {
        app.tick("blink");
        if (app.state("blinkOn") == "true") PASS();
        else FAIL_V("got '%s'", app.state("blinkOn").c_str());
    }

    TEST("10 ticks: back to original (even count)") {
        for (int i = 0; i < 10; i++) app.tick("blink");
        if (app.state("blinkOn") == "true") PASS();
        else FAIL_V("got '%s'", app.state("blinkOn").c_str());
    }

    // ── 5. Complex state updates (weather-style) ──
    printf("\ncomplex state updates (cycling data):\n");

    TEST("initial: Loading...") {
        app.load(WEATHER_STYLE_APP);
        if (app.state("status") == "Loading..." && app.state("temperature") == "--") PASS();
        else FAIL_V("status='%s' temp='%s'", app.state("status").c_str(), app.state("temperature").c_str());
    }

    TEST("tick 1: Moscow 22°") {
        app.tick("updateWeather");
        if (app.state("temperature") == "22" && app.state("status") == "Moscow") PASS();
        else FAIL_V("temp='%s' status='%s'", app.state("temperature").c_str(), app.state("status").c_str());
    }

    TEST("tick 2: London -1°") {
        app.tick("updateWeather");
        if (app.state("temperature") == "-1" && app.state("status") == "London") PASS();
        else FAIL_V("temp='%s' status='%s'", app.state("temperature").c_str(), app.state("status").c_str());
    }

    TEST("tick 3: Tokyo 31°") {
        app.tick("updateWeather");
        if (app.state("temperature") == "31" && app.state("status") == "Tokyo") PASS();
        else FAIL_V("temp='%s' status='%s'", app.state("temperature").c_str(), app.state("status").c_str());
    }

    TEST("tick 4: cycles back to Moscow") {
        app.tick("updateWeather");
        if (app.state("status") == "Moscow" && app.state("updateCount") == "4") PASS();
        else FAIL_V("status='%s' count='%s'", app.state("status").c_str(), app.state("updateCount").c_str());
    }

    // ── Summary ──
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d FAILED ===\n", total - passed, total);
        return 1;
    }
}
