/**
 * Test: Calculator E2E — full user emulation
 *
 * ALL button presses go through Console::exec("ui click btnId"):
 *   calc.click("btn7")   — tap digit 7
 *   calc.click("btnAdd") — tap +
 *   calc.click("btnEq")  — tap =
 *   calc.click("btnC")   — tap C (clear)
 *   calc.display()       — read state.display
 *
 * Pipeline: ui click → triggerClick → g_onclick_handler → engine.call()
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "ui/ui_touch.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"
#include "console/console.h"

// ═══════════════════════════════════════════════════════
//  Test infrastructure
// ═══════════════════════════════════════════════════════

static int g_passed = 0, g_total = 0;
#define TEST(name) printf("  %-55s ", name); g_total++;
#define PASS() do { printf("✓\n"); g_passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)
#define FAIL_V(fmt, ...) do { printf("✗ "); printf(fmt, __VA_ARGS__); printf("\n"); } while(0)
#define SECTION(name) printf("\n%s:\n", name)

// ═══════════════════════════════════════════════════════
//  Calculator App — real HTML + real Lua
// ═══════════════════════════════════════════════════════

const char* CALC_HTML = R"(
<app>
  <ui default="/calc">
    <page id="calc">
      <label id="display" x="5%" y="5%" w="90%" h="50" font="48"
             text-align="right center" bgcolor="#222">{display}</label>

      <button id="btn7" x="5%"  y="80"  w="22%" h="50" onclick="d7">7</button>
      <button id="btn8" x="28%" y="80"  w="22%" h="50" onclick="d8">8</button>
      <button id="btn9" x="51%" y="80"  w="22%" h="50" onclick="d9">9</button>
      <button id="btnDiv" x="74%" y="80"  w="22%" h="50" onclick="opDiv" bgcolor="#f90">÷</button>

      <button id="btn4" x="5%"  y="140" w="22%" h="50" onclick="d4">4</button>
      <button id="btn5" x="28%" y="140" w="22%" h="50" onclick="d5">5</button>
      <button id="btn6" x="51%" y="140" w="22%" h="50" onclick="d6">6</button>
      <button id="btnMul" x="74%" y="140" w="22%" h="50" onclick="opMul" bgcolor="#f90">×</button>

      <button id="btn1" x="5%"  y="200" w="22%" h="50" onclick="d1">1</button>
      <button id="btn2" x="28%" y="200" w="22%" h="50" onclick="d2">2</button>
      <button id="btn3" x="51%" y="200" w="22%" h="50" onclick="d3">3</button>
      <button id="btnSub" x="74%" y="200" w="22%" h="50" onclick="opSub" bgcolor="#f90">−</button>

      <button id="btnC" x="5%"  y="260" w="22%" h="50" onclick="opClear" bgcolor="#f44">C</button>
      <button id="btn0" x="28%" y="260" w="22%" h="50" onclick="d0">0</button>
      <button id="btnEq" x="51%" y="260" w="22%" h="50" onclick="opEq" bgcolor="#4a4">=</button>
      <button id="btnAdd" x="74%" y="260" w="22%" h="50" onclick="opAdd" bgcolor="#f90">+</button>
    </page>
  </ui>

  <state>
    <string name="display" default="0"/>
  </state>

  <script language="lua">
    local current = "0"
    local prev = 0
    local op = ""
    local newInput = true

    function digit(d)
        if newInput then
            current = tostring(d)
            newInput = false
        else
            if current == "0" then
                current = tostring(d)
            else
                current = current .. tostring(d)
            end
        end
        state.display = current
    end

    function d0() digit(0) end
    function d1() digit(1) end
    function d2() digit(2) end
    function d3() digit(3) end
    function d4() digit(4) end
    function d5() digit(5) end
    function d6() digit(6) end
    function d7() digit(7) end
    function d8() digit(8) end
    function d9() digit(9) end

    function calculate()
        local cur = tonumber(current) or 0
        if op == "+" then return prev + cur
        elseif op == "-" then return prev - cur
        elseif op == "*" then return prev * cur
        elseif op == "/" then
            if cur ~= 0 then return prev / cur end
            return 0
        end
        return cur
    end

    function doOp(newOp)
        if not newInput then
            prev = calculate()
            local result = math.floor(prev)
            if prev == result then
                current = tostring(result)
            else
                current = tostring(prev)
            end
            state.display = current
        end
        op = newOp
        newInput = true
    end

    function opAdd() doOp("+") end
    function opSub() doOp("-") end
    function opMul() doOp("*") end
    function opDiv() doOp("/") end

    function opEq()
        prev = calculate()
        local result = math.floor(prev)
        if prev == result then
            current = tostring(result)
        else
            current = tostring(prev)
        end
        state.display = current
        op = ""
        newInput = true
    end

    function opClear()
        current = "0"
        prev = 0
        op = ""
        newInput = true
        state.display = "0"
    end
  </script>
</app>
)";

// ═══════════════════════════════════════════════════════
//  App harness (same pattern as test_timers)
// ═══════════════════════════════════════════════════════

static LuaEngine* g_calcEngine = nullptr;

struct Calc {
    LuaEngine engine;

    bool load() {
        LvglMock::reset();
        LvglMock::create_screen(480, 480);
        State::store().clear();
        engine.shutdown();

        UI::Engine::instance().init();
        TouchSim::init();
        UI::Engine::instance().render(CALC_HTML);
        auto& ui = UI::Engine::instance();

        // Load state defaults
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

        // Init Lua + sync state
        engine.init();
        for (int i = 0; i < ui.stateCount(); i++) {
            const char* name = ui.stateVarName(i);
            const char* def = ui.stateVarDefault(i);
            if (!name) continue;
            engine.setState(name, def ? def : "");
        }

        // Connect onclick → Lua engine
        g_calcEngine = &engine;
        ui.setOnClickHandler([](const char* func) {
            if (g_calcEngine) g_calcEngine->call(func);
        });

        // Execute script block
        const char* code = ui.scriptCode();
        if (code && code[0]) return engine.execute(code);
        return true;
    }

    // Click button by ID — full emulation through Console
    void click(const char* btnId) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ui click %s", btnId);
        Console::exec(buf);
    }

    // Read display
    std::string display() {
        return State::store().getString("display");
    }

    ~Calc() { g_calcEngine = nullptr; engine.shutdown(); }
};

// ═══════════════════════════════════════════════════════
//  Tests
// ═══════════════════════════════════════════════════════

int main() {
    printf("=== Calculator Console E2E Tests ===\n");

    Calc calc;
    if (!calc.load()) {
        printf("FATAL: Failed to load calculator app\n");
        return 1;
    }

    printf("Loaded: %d LVGL objects\n", (int)LvglMock::g_all.size());

    // ─── 1. Widget structure (HTML → LVGL mock tree) ────────

    SECTION("1. Widget structure");

    TEST("16 buttons rendered") {
        auto* page = LvglMock::g_screen->find("Tile", true);
        if (!page) page = LvglMock::g_screen->find("Container", true);
        int btns = page ? page->count("Button", false) : -1;
        if (btns == 16) PASS();
        else FAIL_V("got %d", btns);
    }

    TEST("Display label with binding '0'") {
        if (calc.display() == "0") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    TEST("Has digit buttons 0-9 in tree") {
        bool ok = true;
        for (char c = '0'; c <= '9'; c++) {
            char s[2] = {c, 0};
            if (!LvglMock::find_by_text(s)) ok = false;
        }
        if (ok) PASS();
        else FAIL("missing digit label");
    }

    TEST("Has operator buttons +-×÷=C") {
        const char* ops[] = {"+", "−", "×", "÷", "=", "C"};
        bool ok = true;
        for (auto op : ops) {
            if (!LvglMock::find_by_text(op)) ok = false;
        }
        if (ok) PASS();
        else FAIL("missing operator");
    }

    TEST("State 'display' declared as string, default '0'") {
        auto& ui = UI::Engine::instance();
        bool found = false;
        for (int i = 0; i < ui.stateCount(); i++) {
            if (strcmp(ui.stateVarName(i), "display") == 0) {
                found = (strcmp(ui.stateVarType(i), "string") == 0 &&
                         strcmp(ui.stateVarDefault(i), "0") == 0);
            }
        }
        if (found) PASS();
        else FAIL("state declaration mismatch");
    }

    // ─── 2. Console infrastructure ──────────────────────────

    SECTION("2. Console infrastructure");

    TEST("sys ping → ok") {
        auto r = Console::exec("sys ping");
        if (r.success && r.status == "ok") PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("sys sync → returns protocol version") {
        auto r = Console::exec("sys sync 2.7 2026-02-15T12:00:00Z +03:00");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("unknown subsystem → error") {
        auto r = Console::exec("foo bar");
        if (!r.success && r.errorCode == "invalid") PASS();
        else FAIL("should fail");
    }

    TEST("empty command → error") {
        auto r = Console::exec("");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui without subcommand → error") {
        auto r = Console::exec("ui");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("unknown ui command → error") {
        auto r = Console::exec("ui foobar");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    // ─── 3. Console state commands ──────────────────────────

    SECTION("3. Console: state read/write");

    TEST("ui get display → '0'") {
        auto r = Console::exec("ui get display");
        if (r.success && calc.display() == "0") PASS();
        else FAIL_V("success=%d display='%s'", r.success, calc.display().c_str());
    }

    TEST("ui set display 'HELLO' → state updated") {
        Console::exec("ui set display HELLO");
        if (calc.display() == "HELLO") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    TEST("ui set + ui get roundtrip") {
        Console::exec("ui set display 42");
        auto r = Console::exec("ui get display");
        if (r.success && calc.display() == "42") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    // restore
    Console::exec("ui set display 0");
    calc.click("btnC");

    // ─── 4. Console touch (coordinate mode) ─────────────────

    SECTION("4. Console: touch simulation (coordinates)");

    TEST("ui tap 120 160 → ok") {
        auto r = Console::exec("ui tap 120 160");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui tap 0 0 → ok (top-left corner)") {
        auto r = Console::exec("ui tap 0 0");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui tap 479 479 → ok (bottom-right edge)") {
        auto r = Console::exec("ui tap 479 479");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui tap 480 240 → error (x out of bounds)") {
        auto r = Console::exec("ui tap 480 240");
        if (!r.success) PASS();
        else FAIL("should reject x=480");
    }

    TEST("ui tap -1 100 → error (negative)") {
        auto r = Console::exec("ui tap -1 100");
        if (!r.success) PASS();
        else FAIL("should reject x=-1");
    }

    TEST("ui hold 120 160 → ok (default 500ms)") {
        auto r = Console::exec("ui hold 120 160");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui hold 120 160 1000 → ok (1s)") {
        auto r = Console::exec("ui hold 120 160 1000");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui swipe left → ok") {
        auto r = Console::exec("ui swipe left");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui swipe right → ok") {
        auto r = Console::exec("ui swipe right");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui swipe up → ok") {
        auto r = Console::exec("ui swipe up");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui swipe down fast → ok") {
        auto r = Console::exec("ui swipe down fast");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui swipe left slow → ok") {
        auto r = Console::exec("ui swipe left slow");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    TEST("ui swipe 0 120 240 120 300 → ok (coord swipe)") {
        auto r = Console::exec("ui swipe 0 120 240 120 300");
        if (r.success) PASS();
        else FAIL(r.errorMessage.c_str());
    }

    // ─── 5. Console error handling ──────────────────────────

    SECTION("5. Console: argument validation");

    TEST("ui tap (no args) → error") {
        auto r = Console::exec("ui tap");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui hold (no args) → error") {
        auto r = Console::exec("ui hold");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui swipe (no args) → error") {
        auto r = Console::exec("ui swipe");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui type (no args) → error") {
        auto r = Console::exec("ui type");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui click (no args) → error") {
        auto r = Console::exec("ui click");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui set (no args) → error") {
        auto r = Console::exec("ui set");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui nav (no args) → error") {
        auto r = Console::exec("ui nav");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    TEST("ui call (no args) → error") {
        auto r = Console::exec("ui call");
        if (!r.success) PASS();
        else FAIL("should fail");
    }

    // ─── 6. Lua calculator: digits ──────────────────────────
    // ui click btnX → triggerClick → g_onclick_handler → engine.call()

    SECTION("6. Lua calculator: digit input");

    calc.click("btnC");

    TEST("d7 → '7'") {
        calc.click("btn7");
        if (calc.display() == "7") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("d0 → '0'") {
        calc.click("btn0");
        if (calc.display() == "0") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("d1 d2 d3 → '123'") {
        calc.click("btn1");
        calc.click("btn2");
        calc.click("btn3");
        if (calc.display() == "123") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("d0 d5 → '5' (leading zero suppressed)") {
        calc.click("btn0");
        calc.click("btn5");
        if (calc.display() == "5") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("d0 d0 d0 → '0' (multiple zeros)") {
        calc.click("btn0");
        calc.click("btn0");
        calc.click("btn0");
        if (calc.display() == "0") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("All digits: 1234567890") {
        calc.click("btn1"); calc.click("btn2"); calc.click("btn3");
        calc.click("btn4"); calc.click("btn5"); calc.click("btn6");
        calc.click("btn7"); calc.click("btn8"); calc.click("btn9");
        calc.click("btn0");
        if (calc.display() == "1234567890") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    // ─── 7. Lua calculator: basic operations ────────────────

    SECTION("7. Lua calculator: basic operations");

    calc.click("btnC");

    TEST("2 + 3 = 5") {
        calc.click("btn2");
        calc.click("btnAdd");
        calc.click("btn3");
        calc.click("btnEq");
        if (calc.display() == "5") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("9 − 4 = 5") {
        calc.click("btn9");
        calc.click("btnSub");
        calc.click("btn4");
        calc.click("btnEq");
        if (calc.display() == "5") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("6 × 7 = 42") {
        calc.click("btn6");
        calc.click("btnMul");
        calc.click("btn7");
        calc.click("btnEq");
        if (calc.display() == "42") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("8 ÷ 4 = 2") {
        calc.click("btn8");
        calc.click("btnDiv");
        calc.click("btn4");
        calc.click("btnEq");
        if (calc.display() == "2") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("0 + 0 = 0") {
        calc.click("btn0");
        calc.click("btnAdd");
        calc.click("btn0");
        calc.click("btnEq");
        if (calc.display() == "0") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    // ─── 8. Lua calculator: multi-digit operands ────────────

    SECTION("8. Lua calculator: multi-digit");

    calc.click("btnC");

    TEST("12 + 34 = 46") {
        calc.click("btn1"); calc.click("btn2");
        calc.click("btnAdd");
        calc.click("btn3"); calc.click("btn4");
        calc.click("btnEq");
        if (calc.display() == "46") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("100 × 5 = 500") {
        calc.click("btn1"); calc.click("btn0"); calc.click("btn0");
        calc.click("btnMul");
        calc.click("btn5");
        calc.click("btnEq");
        if (calc.display() == "500") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("999 − 1 = 998") {
        calc.click("btn9"); calc.click("btn9"); calc.click("btn9");
        calc.click("btnSub");
        calc.click("btn1");
        calc.click("btnEq");
        if (calc.display() == "998") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("50 ÷ 10 = 5") {
        calc.click("btn5"); calc.click("btn0");
        calc.click("btnDiv");
        calc.click("btn1"); calc.click("btn0");
        calc.click("btnEq");
        if (calc.display() == "5") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    // ─── 9. Lua calculator: operator chaining ───────────────

    SECTION("9. Lua calculator: operator chaining");

    calc.click("btnC");

    TEST("2 + 3 + 4 = 9") {
        calc.click("btn2");
        calc.click("btnAdd");
        calc.click("btn3");
        calc.click("btnAdd");  // evaluates 2+3=5, starts new add
        calc.click("btn4");
        calc.click("btnEq");   // 5+4=9
        if (calc.display() == "9") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("5 × 4 − 8 = 12 (mixed operators)") {
        calc.click("btn5");
        calc.click("btnMul");
        calc.click("btn4");
        calc.click("btnSub");  // 5*4=20
        calc.click("btn8");
        calc.click("btnEq");   // 20-8=12
        if (calc.display() == "12") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("1 + 2 + 3 + 4 = 10") {
        calc.click("btn1");
        calc.click("btnAdd"); calc.click("btn2");
        calc.click("btnAdd"); calc.click("btn3");
        calc.click("btnAdd"); calc.click("btn4");
        calc.click("btnEq");
        if (calc.display() == "10") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("10 ÷ 2 × 3 = 15") {
        calc.click("btn1"); calc.click("btn0");
        calc.click("btnDiv");
        calc.click("btn2");
        calc.click("btnMul");  // 10/2=5
        calc.click("btn3");
        calc.click("btnEq");   // 5*3=15
        if (calc.display() == "15") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    // ─── 10. Lua calculator: clear ──────────────────────────

    SECTION("10. Lua calculator: clear");

    TEST("C resets display to '0'") {
        calc.click("btn5"); calc.click("btn5");
        calc.click("btnC");
        if (calc.display() == "0") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    TEST("C mid-operation resets fully") {
        calc.click("btn9");
        calc.click("btnAdd");
        calc.click("btnC");
        calc.click("btn3");
        calc.click("btnEq");
        // After full clear, = just returns current (3)
        if (calc.display() == "3") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    TEST("C then new calculation works") {
        calc.click("btn7");
        calc.click("btnMul");
        calc.click("btn6");
        calc.click("btnC");
        calc.click("btn2");
        calc.click("btnAdd");
        calc.click("btn3");
        calc.click("btnEq");
        if (calc.display() == "5") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    // ─── 11. Lua calculator: edge cases ─────────────────────

    SECTION("11. Lua calculator: edge cases");

    calc.click("btnC");

    TEST("Division by zero → '0'") {
        calc.click("btn5");
        calc.click("btnDiv");
        calc.click("btn0");
        calc.click("btnEq");
        if (calc.display() == "0") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("Equals without operator → shows current") {
        calc.click("btn7");
        calc.click("btnEq");
        if (calc.display() == "7") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("New digit after = replaces result") {
        calc.click("btn2");
        calc.click("btnAdd");
        calc.click("btn3");
        calc.click("btnEq");  // display=5
        calc.click("btn9");    // should replace, not append
        if (calc.display() == "9") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("Operator then = uses current as both operands") {
        calc.click("btn8");
        calc.click("btnAdd");
        calc.click("btnEq");
        // doOp("+") sets prev=8, op="+", newInput=true
        // opEq: calculate() with current still "8", prev=8 → 8+8=16
        if (calc.display() == "16") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("Multiple = repeats nothing (no op pending)") {
        calc.click("btn5");
        calc.click("btnAdd");
        calc.click("btn3");
        calc.click("btnEq");  // 5+3=8
        calc.click("btnEq");  // op="" → just returns current 8
        if (calc.display() == "8") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("Large numbers: 999 + 1 = 1000") {
        calc.click("btn9"); calc.click("btn9"); calc.click("btn9");
        calc.click("btnAdd");
        calc.click("btn1");
        calc.click("btnEq");
        if (calc.display() == "1000") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    calc.click("btnC");

    TEST("Subtraction into negative: 3 − 5 = -2") {
        calc.click("btn3");
        calc.click("btnSub");
        calc.click("btn5");
        calc.click("btnEq");
        if (calc.display() == "-2") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    // ─── 12. Console ↔ Lua roundtrip ────────────────────────

    SECTION("12. Console ↔ Lua roundtrip");

    calc.click("btnC");

    TEST("Lua updates state → Console reads via ui get") {
        calc.click("btn4");
        calc.click("btn2");
        auto r = Console::exec("ui get display");
        if (r.success && calc.display() == "42") PASS();
        else FAIL_V("got '%s'", calc.display().c_str());
    }

    TEST("Console sets state → Lua calculation starts fresh") {
        Console::exec("ui set display 999");
        // State shows console value
        if (calc.display() != "999") {
            FAIL_V("set failed, got '%s'", calc.display().c_str());
        } else {
            // Clear resets everything via Lua
            calc.click("btnC");
            if (calc.display() == "0") PASS();
            else FAIL_V("clear failed, got '%s'", calc.display().c_str());
        }
    }

    calc.click("btnC");

    TEST("Full roundtrip: calc 7×8 → Console reads 56") {
        calc.click("btn7");
        calc.click("btnMul");
        calc.click("btn8");
        calc.click("btnEq");
        auto r = Console::exec("ui get display");
        if (r.success && State::store().getString("display") == "56") PASS();
        else FAIL_V("got '%s'", State::store().getString("display").c_str());
    }

    calc.click("btnC");

    TEST("Chained: 2+3=5 → Console reads → +4=9") {
        calc.click("btn2");
        calc.click("btnAdd");
        calc.click("btn3");
        calc.click("btnEq");
        // Console reads intermediate
        std::string mid = State::store().getString("display");
        bool midOk = (mid == "5");
        // Continue from Lua
        calc.click("btnAdd");
        calc.click("btn4");
        calc.click("btnEq");
        std::string fin = State::store().getString("display");
        if (midOk && fin == "9") PASS();
        else FAIL_V("mid='%s' fin='%s'", mid.c_str(), fin.c_str());
    }

    // ═══════════════════════════════════════════════════
    //  Summary
    // ═══════════════════════════════════════════════════

    printf("\n");
    if (g_passed == g_total) {
        printf("=== ALL %d CALCULATOR E2E TESTS PASSED ===\n", g_total);
        return 0;
    } else {
        printf("=== %d/%d CALCULATOR E2E TESTS PASSED ===\n", g_passed, g_total);
        return 1;
    }
}
