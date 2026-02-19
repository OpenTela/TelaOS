/**
 * Test: Console Command Argument Parsing
 *
 * Exhaustive regression test for every command with every argument variation.
 * Covers two previously-broken areas:
 *
 *   1. Auto-detect: widget ID vs coordinates (isIntStr fix)
 *      "btn7" → widget mode, "120" → coordinate mode
 *
 *   2. Quoted strings in line parser
 *      ui set myVar "Hello World" → stores "Hello World" not "Hello"
 *
 * Structure mirrors Console::exec dispatch order:
 *   1. ui set/get/text/nav/call/notify
 *   2. ui tap/click  (auto-detect: int→coords, string→widgetId)
 *   3. ui hold       (auto-detect + duration)
 *   4. ui swipe      (direction vs coords)
 *   5. ui type       (1-arg vs 2-arg + quotes)
 *   6. sys ping/info/time/sync
 *   7. line parsing edge cases
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "ui/ui_touch.h"
#include "core/state_store.h"
#include "console/console.h"

// ═══════════════════════════════════════════════════════
//  Test infrastructure
// ═══════════════════════════════════════════════════════

static int g_passed = 0, g_total = 0;
#define TEST(name)     printf("  %-60s ", name); g_total++;
#define PASS()         do { printf("✓\n"); g_passed++; } while(0)
#define FAIL(msg)      printf("✗ %s\n", msg)
#define FAIL_V(f, ...) do { printf("✗ "); printf(f, __VA_ARGS__); printf("\n"); } while(0)
#define SECTION(name)  printf("\n%s:\n", name)

// Shorthand
static Console::Result R(const char* cmd) { return Console::exec(cmd); }
static bool ok(const char* cmd)  { return Console::exec(cmd).success; }
static bool err(const char* cmd) { return !Console::exec(cmd).success; }
static std::string state(const char* key) { return State::store().getString(key); }

// ═══════════════════════════════════════════════════════
//  Setup: minimal app with diverse widget IDs
// ═══════════════════════════════════════════════════════

static void setup() {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    State::store().clear();
    UI::Engine::instance().init();
    TouchSim::init();

    // Widget IDs designed to stress auto-detect:
    //   "btn7"     - letters first, digit inside (was broken: atoi→0)
    //   "btnPlus"  - pure letters (was broken: atoi→0)
    //   "7btn"     - digit first (was broken: atoi→7)
    //   "123abc"   - digits then letters (was broken: atoi→123)
    //   "editInput"- normal camelCase ID
    const char* html = R"(
    <app>
      <ui default="/p">
        <page id="p">
          <button id="btn7" x="10" y="10" w="50" h="50" onclick="d7">7</button>
          <button id="btnPlus" x="70" y="10" w="50" h="50" onclick="opAdd">+</button>
          <button id="7btn" x="10" y="70" w="50" h="50" onclick="foo">X</button>
          <button id="123abc" x="70" y="70" w="50" h="50" onclick="bar">Y</button>
          <input id="editInput" x="10" y="130" w="200" h="40" bind="inputVal"
                 placeholder="Type here"/>
          <label id="display" x="10" y="180" w="200" h="40">{displayVal}</label>
        </page>
      </ui>
      <state>
        <string name="inputVal" default=""/>
        <string name="displayVal" default=""/>
        <string name="myVar" default=""/>
      </state>
    </app>
    )";

    UI::Engine::instance().render(html);
    State::store().set("inputVal", "");
    State::store().set("displayVal", "");
    State::store().set("myVar", "");
}

// ═══════════════════════════════════════════════════════
//  Tests
// ═══════════════════════════════════════════════════════

int main() {
    printf("=== Console Argument Parsing Tests ===\n");
    setup();

    // ─── 1. ui set ──────────────────────────────────────────

    SECTION("1. ui set");

    TEST("ui set myVar hello → 'hello'") {
        ok("ui set myVar hello");
        if (state("myVar") == "hello") PASS();
        else FAIL_V("got '%s'", state("myVar").c_str());
    }

    TEST("ui set myVar 42 → '42'") {
        ok("ui set myVar 42");
        if (state("myVar") == "42") PASS();
        else FAIL_V("got '%s'", state("myVar").c_str());
    }

    TEST("ui set myVar 0 → '0'") {
        ok("ui set myVar 0");
        if (state("myVar") == "0") PASS();
        else FAIL_V("got '%s'", state("myVar").c_str());
    }

    TEST("ui set myVar → empty value") {
        ok("ui set myVar something");
        ok("ui set myVar");
        if (state("myVar") == "") PASS();
        else FAIL_V("got '%s'", state("myVar").c_str());
    }

    TEST("ui set → error (no args)") {
        if (err("ui set")) PASS();
        else FAIL("");
    }

    TEST("ui set myVar \"Hello World\" → stores with space [QUOTE FIX]") {
        ok("ui set myVar \"Hello World\"");
        if (state("myVar") == "Hello World") PASS();
        else FAIL_V("got '%s'", state("myVar").c_str());
    }

    TEST("ui set myVar \"two words\" → stores 'two words'") {
        ok("ui set myVar \"two words\"");
        if (state("myVar") == "two words") PASS();
        else FAIL_V("got '%s'", state("myVar").c_str());
    }

    TEST("ui set myVar \"\" → empty quoted string") {
        ok("ui set myVar something");
        ok("ui set myVar \"\"");
        if (state("myVar") == "") PASS();
        else FAIL_V("got '%s'", state("myVar").c_str());
    }

    TEST("ui set var-with-dashes ok → works") {
        if (ok("ui set var-with-dashes ok")) PASS();
        else FAIL("");
    }

    TEST("ui set UPPER lower → case preserved") {
        ok("ui set UPPER lower");
        if (state("UPPER") == "lower") PASS();
        else FAIL("");
    }

    // ─── 2. ui get ──────────────────────────────────────────

    SECTION("2. ui get");

    TEST("ui get existingVar → ok") {
        State::store().set("testGet", "val123");
        if (ok("ui get testGet")) PASS();
        else FAIL("");
    }

    TEST("ui get nonExistent → ok (returns empty)") {
        if (ok("ui get nonExistentVar12345")) PASS();
        else FAIL("");
    }

    TEST("ui get → error") {
        if (err("ui get")) PASS();
        else FAIL("");
    }

    // ─── 3. ui text / nav / call / notify ───────────────────

    SECTION("3. ui text / nav / call / notify");

    TEST("ui text displayVal hello → ok") {
        ok("ui text displayVal hello");
        if (state("displayVal") == "hello") PASS();
        else FAIL("");
    }

    TEST("ui text → error") {
        if (err("ui text")) PASS();
        else FAIL("");
    }

    TEST("ui nav settings → ok") {
        if (ok("ui nav settings")) PASS();
        else FAIL("");
    }

    TEST("ui nav → error") {
        if (err("ui nav")) PASS();
        else FAIL("");
    }

    TEST("ui call myFunction → ok") {
        if (ok("ui call myFunction")) PASS();
        else FAIL("");
    }

    TEST("ui call → error") {
        if (err("ui call")) PASS();
        else FAIL("");
    }

    TEST("ui notify Title Message → ok") {
        if (ok("ui notify Title Message")) PASS();
        else FAIL("");
    }

    TEST("ui notify \"Long Title\" \"Long Message\" → ok [QUOTE]") {
        if (ok("ui notify \"Long Title\" \"Long Message\"")) PASS();
        else FAIL("");
    }

    // ─── 4. ui tap / click — COORDINATE MODE ────────────────

    SECTION("4. ui tap/click — coordinates");

    TEST("ui tap 120 160 → ok") {
        if (ok("ui tap 120 160")) PASS();
        else FAIL("");
    }

    TEST("ui click 120 160 → ok (alias)") {
        if (ok("ui click 120 160")) PASS();
        else FAIL("");
    }

    TEST("ui tap 0 0 → ok (origin)") {
        if (ok("ui tap 0 0")) PASS();
        else FAIL("");
    }

    TEST("ui tap 479 479 → ok (max valid)") {
        if (ok("ui tap 479 479")) PASS();
        else FAIL("");
    }

    TEST("ui tap 480 0 → error (x out of bounds)") {
        if (err("ui tap 480 0")) PASS();
        else FAIL("");
    }

    TEST("ui tap 0 480 → error (y out of bounds)") {
        if (err("ui tap 0 480")) PASS();
        else FAIL("");
    }

    TEST("ui tap -1 100 → widget mode, '-1' not found") {
        // isIntStr("-1")=true → argInt=-1 → widget mode
        // widget "-1" doesn't exist → error
        if (err("ui tap -1 100")) PASS();
        else FAIL("");
    }

    TEST("ui tap → error (no args)") {
        if (err("ui tap")) PASS();
        else FAIL("");
    }

    TEST("ui click → error (no args)") {
        if (err("ui click")) PASS();
        else FAIL("");
    }

    TEST("ui tap 120 → error (only x)") {
        if (err("ui tap 120")) PASS();
        else FAIL("missing y should fail");
    }

    // ─── 5. ui tap / click — WIDGET ID MODE [AUTODETECT FIX] ──

    SECTION("5. ui tap/click — widget ID auto-detect");

    TEST("ui tap btn7 → ok (letters-first ID)") {
        if (ok("ui tap btn7")) PASS();
        else FAIL(R("ui tap btn7").errorMessage.c_str());
    }

    TEST("ui click btn7 → ok (click alias)") {
        if (ok("ui click btn7")) PASS();
        else FAIL(R("ui click btn7").errorMessage.c_str());
    }

    TEST("ui tap btnPlus → ok (pure letters ID)") {
        if (ok("ui tap btnPlus")) PASS();
        else FAIL(R("ui tap btnPlus").errorMessage.c_str());
    }

    TEST("ui click btnPlus → ok") {
        if (ok("ui click btnPlus")) PASS();
        else FAIL("");
    }

    TEST("ui tap 7btn → ok (digit-first ID, not a pure number)") {
        // isIntStr("7btn") = false → widget mode → found
        if (ok("ui tap 7btn")) PASS();
        else FAIL(R("ui tap 7btn").errorMessage.c_str());
    }

    TEST("ui tap 123abc → ok (digits-then-letters ID)") {
        // isIntStr("123abc") = false → widget mode → found
        if (ok("ui tap 123abc")) PASS();
        else FAIL(R("ui tap 123abc").errorMessage.c_str());
    }

    TEST("ui tap display → ok (label widget)") {
        if (ok("ui tap display")) PASS();
        else FAIL(R("ui tap display").errorMessage.c_str());
    }

    TEST("ui tap editInput → ok (input widget)") {
        if (ok("ui tap editInput")) PASS();
        else FAIL(R("ui tap editInput").errorMessage.c_str());
    }

    TEST("ui tap nonExistent → error (widget not found)") {
        auto r = R("ui tap nonExistent");
        if (!r.success && r.errorMessage.find("not found") != std::string::npos) PASS();
        else FAIL_V("expected 'not found', got '%s'", r.errorMessage.c_str());
    }

    TEST("ui click NOPE → error (widget not found)") {
        if (err("ui click NOPE")) PASS();
        else FAIL("");
    }

    // ─── 6. ui hold — coords + widget ID + duration ─────────

    SECTION("6. ui hold — coordinates");

    TEST("ui hold 120 160 → ok (default 500ms)") {
        if (ok("ui hold 120 160")) PASS();
        else FAIL("");
    }

    TEST("ui hold 120 160 1000 → ok (1s)") {
        if (ok("ui hold 120 160 1000")) PASS();
        else FAIL("");
    }

    TEST("ui hold 120 160 0 → ok (0ms)") {
        if (ok("ui hold 120 160 0")) PASS();
        else FAIL("");
    }

    TEST("ui hold 0 0 → ok (origin)") {
        if (ok("ui hold 0 0")) PASS();
        else FAIL("");
    }

    TEST("ui hold 479 479 → ok (max)") {
        if (ok("ui hold 479 479")) PASS();
        else FAIL("");
    }

    TEST("ui hold 480 0 → error") {
        if (err("ui hold 480 0")) PASS();
        else FAIL("");
    }

    TEST("ui hold → error") {
        if (err("ui hold")) PASS();
        else FAIL("");
    }

    TEST("ui hold 120 → error (only x)") {
        if (err("ui hold 120")) PASS();
        else FAIL("");
    }

    SECTION("6b. ui hold — widget ID [AUTODETECT FIX]");

    TEST("ui hold btn7 → ok (default 500ms)") {
        if (ok("ui hold btn7")) PASS();
        else FAIL(R("ui hold btn7").errorMessage.c_str());
    }

    TEST("ui hold btn7 1000 → ok (1s)") {
        if (ok("ui hold btn7 1000")) PASS();
        else FAIL(R("ui hold btn7 1000").errorMessage.c_str());
    }

    TEST("ui hold btnPlus 500 → ok") {
        if (ok("ui hold btnPlus 500")) PASS();
        else FAIL(R("ui hold btnPlus 500").errorMessage.c_str());
    }

    TEST("ui hold 7btn → ok (digit-first ID)") {
        if (ok("ui hold 7btn")) PASS();
        else FAIL(R("ui hold 7btn").errorMessage.c_str());
    }

    TEST("ui hold 123abc 2000 → ok (digits-then-letters)") {
        if (ok("ui hold 123abc 2000")) PASS();
        else FAIL(R("ui hold 123abc 2000").errorMessage.c_str());
    }

    TEST("ui hold nonExistent → error") {
        if (err("ui hold nonExistent")) PASS();
        else FAIL("");
    }

    TEST("ui hold nonExistent 1000 → error") {
        if (err("ui hold nonExistent 1000")) PASS();
        else FAIL("");
    }

    // ─── 7. ui swipe — directions ───────────────────────────

    SECTION("7. ui swipe — directions");

    TEST("ui swipe left → ok")        { if (ok("ui swipe left")) PASS(); else FAIL(""); }
    TEST("ui swipe right → ok")       { if (ok("ui swipe right")) PASS(); else FAIL(""); }
    TEST("ui swipe up → ok")          { if (ok("ui swipe up")) PASS(); else FAIL(""); }
    TEST("ui swipe down → ok")        { if (ok("ui swipe down")) PASS(); else FAIL(""); }
    TEST("ui swipe l → ok (short)")   { if (ok("ui swipe l")) PASS(); else FAIL(""); }
    TEST("ui swipe r → ok (short)")   { if (ok("ui swipe r")) PASS(); else FAIL(""); }
    TEST("ui swipe u → ok (short)")   { if (ok("ui swipe u")) PASS(); else FAIL(""); }
    TEST("ui swipe d → ok (short)")   { if (ok("ui swipe d")) PASS(); else FAIL(""); }

    SECTION("7b. ui swipe — speed modifiers");

    TEST("ui swipe left fast → ok")   { if (ok("ui swipe left fast")) PASS(); else FAIL(""); }
    TEST("ui swipe left slow → ok")   { if (ok("ui swipe left slow")) PASS(); else FAIL(""); }
    TEST("ui swipe right fast → ok")  { if (ok("ui swipe right fast")) PASS(); else FAIL(""); }
    TEST("ui swipe up slow → ok")     { if (ok("ui swipe up slow")) PASS(); else FAIL(""); }
    TEST("ui swipe down fast → ok")   { if (ok("ui swipe down fast")) PASS(); else FAIL(""); }
    TEST("ui swipe left normal → ok (default speed)") {
        if (ok("ui swipe left normal")) PASS();
        else FAIL("");
    }

    SECTION("7c. ui swipe — coordinate mode");

    TEST("ui swipe 0 120 240 120 → ok (horizontal)") {
        if (ok("ui swipe 0 120 240 120")) PASS();
        else FAIL("");
    }

    TEST("ui swipe 120 0 120 240 → ok (vertical)") {
        if (ok("ui swipe 120 0 120 240")) PASS();
        else FAIL("");
    }

    TEST("ui swipe 0 0 479 479 300 → ok (diagonal + ms)") {
        if (ok("ui swipe 0 0 479 479 300")) PASS();
        else FAIL("");
    }

    TEST("ui swipe 0 0 0 0 → ok (zero-length)") {
        if (ok("ui swipe 0 0 0 0")) PASS();
        else FAIL("");
    }

    TEST("ui swipe 100 200 → error (only 2 coords)") {
        if (err("ui swipe 100 200")) PASS();
        else FAIL("");
    }

    TEST("ui swipe 100 200 300 → error (only 3 coords)") {
        if (err("ui swipe 100 200 300")) PASS();
        else FAIL("");
    }

    TEST("ui swipe → error") {
        if (err("ui swipe")) PASS();
        else FAIL("");
    }

    TEST("ui swipe LEFT → error (case-sensitive)") {
        if (err("ui swipe LEFT")) PASS();
        else FAIL("");
    }

    TEST("ui swipe diagonal → error (unknown direction)") {
        if (err("ui swipe diagonal")) PASS();
        else FAIL("");
    }

    // ─── 8. ui type — quotes + widget ID ────────────────────
    // Note: type always fails at the lv_obj_check_type mock level.
    // We test dispatch logic by checking error messages.

    SECTION("8. ui type — argument dispatch");

    TEST("ui type → error (no args)") {
        if (err("ui type")) PASS();
        else FAIL("");
    }

    TEST("ui type Hello → 1-arg path (no focus in mock)") {
        auto r = R("ui type Hello");
        // Expected: fails because no focused textarea, not because of parsing
        if (!r.success && r.errorMessage.find("focus") != std::string::npos) PASS();
        else FAIL_V("wrong error: '%s'", r.errorMessage.c_str());
    }

    TEST("ui type \"Hello World\" → 1-arg, quoted [QUOTE FIX]") {
        auto r = R("ui type \"Hello World\"");
        // 1 arg after quotes → text-only path → "no focus" error
        if (!r.success && r.errorMessage.find("focus") != std::string::npos) PASS();
        else FAIL_V("wrong error: '%s'", r.errorMessage.c_str());
    }

    TEST("ui type editInput Hello → 2-arg, widget+text path") {
        auto r = R("ui type editInput Hello");
        // 2 args → widget mode: focusInput("editInput")
        // fails at lv_obj_check_type mock → "not found or not an input"
        if (!r.success && r.errorMessage.find("not") != std::string::npos) PASS();
        else FAIL_V("wrong error: '%s'", r.errorMessage.c_str());
    }

    TEST("ui type editInput \"Hello World\" → 2-arg, quoted text [QUOTE FIX]") {
        auto r = R("ui type editInput \"Hello World\"");
        // tokens: ["editInput", "Hello World"] → 2 args → widget mode
        if (!r.success && r.errorMessage.find("not") != std::string::npos) PASS();
        else FAIL_V("wrong error: '%s'", r.errorMessage.c_str());
    }

    TEST("ui type a → 1-arg, single char") {
        auto r = R("ui type a");
        if (!r.success && r.errorMessage.find("focus") != std::string::npos) PASS();
        else FAIL_V("wrong error: '%s'", r.errorMessage.c_str());
    }

    // ─── 9. sys commands ────────────────────────────────────

    SECTION("9. sys ping / info / time / reboot");

    TEST("sys ping → ok")    { if (ok("sys ping")) PASS(); else FAIL(""); }
    TEST("sys info → ok")    { if (ok("sys info")) PASS(); else FAIL(""); }
    TEST("sys time → ok")    { if (ok("sys time")) PASS(); else FAIL(""); }
    TEST("sys time 1700000000 → ok") { if (ok("sys time 1700000000")) PASS(); else FAIL(""); }
    TEST("sys reboot → ok")  { if (ok("sys reboot")) PASS(); else FAIL(""); }

    // ─── 10. sys sync — datetime + timezone ─────────────────

    SECTION("10. sys sync");

    TEST("sys sync 2.7 2026-02-15T12:00:00Z +03:00 → ok") {
        if (ok("sys sync 2.7 2026-02-15T12:00:00Z +03:00")) PASS();
        else FAIL("");
    }

    TEST("sys sync 2.7 2026-02-15T12:00:00 +03:00 → ok (no Z)") {
        if (ok("sys sync 2.7 2026-02-15T12:00:00 +03:00")) PASS();
        else FAIL("");
    }

    TEST("sys sync 2.7 2026-01-01T00:00:00 +00:00 → ok (midnight UTC)") {
        if (ok("sys sync 2.7 2026-01-01T00:00:00 +00:00")) PASS();
        else FAIL("");
    }

    TEST("sys sync 2.7 2026-12-31T23:59:59 -12:00 → ok (extreme tz)") {
        if (ok("sys sync 2.7 2026-12-31T23:59:59 -12:00")) PASS();
        else FAIL("");
    }

    TEST("sys sync 2.7 2026-06-15T08:30:00 +05:30 → ok (half-hour tz)") {
        if (ok("sys sync 2.7 2026-06-15T08:30:00 +05:30")) PASS();
        else FAIL("");
    }

    TEST("sys sync → ok (no args, just returns versions)") {
        if (ok("sys sync")) PASS();
        else FAIL("");
    }

    TEST("sys sync 2.7 → ok (version only)") {
        if (ok("sys sync 2.7")) PASS();
        else FAIL("");
    }

    TEST("sys sync 2.7 2026-02-15T12:00:00 → ok (no tz)") {
        if (ok("sys sync 2.7 2026-02-15T12:00:00")) PASS();
        else FAIL("");
    }

    // ─── 11. unknown / malformed ────────────────────────────

    SECTION("11. Error handling");

    TEST("foo → error")           { if (err("foo")) PASS(); else FAIL(""); }
    TEST("foo bar → error")       { if (err("foo bar")) PASS(); else FAIL(""); }
    TEST("ui foobar → error")     { if (err("ui foobar")) PASS(); else FAIL(""); }
    TEST("sys foobar → error")    { if (err("sys foobar")) PASS(); else FAIL(""); }
    TEST("app foobar → error")    { if (err("app foobar")) PASS(); else FAIL(""); }
    TEST("(empty) → error")       { if (err("")) PASS(); else FAIL(""); }
    TEST("whitespace → error")    { if (err("   ")) PASS(); else FAIL(""); }

    // ─── 12. boundary values ────────────────────────────────

    SECTION("12. Boundary values");

    TEST("ui tap 0 0 → ok")                { if (ok("ui tap 0 0")) PASS(); else FAIL(""); }
    TEST("ui tap 479 479 → ok")            { if (ok("ui tap 479 479")) PASS(); else FAIL(""); }
    TEST("ui tap 480 480 → error")         { if (err("ui tap 480 480")) PASS(); else FAIL(""); }
    TEST("ui tap 999999 0 → error")        { if (err("ui tap 999999 0")) PASS(); else FAIL(""); }
    TEST("ui hold 0 0 0 → ok (0ms)")       { if (ok("ui hold 0 0 0")) PASS(); else FAIL(""); }
    TEST("ui hold 0 0 99999 → ok (huge)")  { if (ok("ui hold 0 0 99999")) PASS(); else FAIL(""); }
    TEST("ui swipe 0 0 0 0 → ok (zero)")   { if (ok("ui swipe 0 0 0 0")) PASS(); else FAIL(""); }
    TEST("ui swipe 0 0 479 479 1 → ok")    { if (ok("ui swipe 0 0 479 479 1")) PASS(); else FAIL(""); }

    // ─── 13. Auto-detect discrimination ────────────────────
    // Critical regression zone: isIntStr must correctly distinguish
    // pure integers (→ coordinates) from widget IDs (→ getWidgetCenter).
    //
    // Key invariant: "7 8" = coordinates, "7btn" = widget ID

    SECTION("13. Auto-detect: coordinates stay coordinates");

    TEST("ui tap 7 8 → coord mode (both pure ints)") {
        if (ok("ui tap 7 8")) PASS();
        else FAIL("small numbers must be coordinates");
    }

    TEST("ui tap 1 1 → coord mode") {
        if (ok("ui tap 1 1")) PASS();
        else FAIL("");
    }

    TEST("ui tap 0 0 → coord mode") {
        if (ok("ui tap 0 0")) PASS();
        else FAIL("");
    }

    TEST("ui tap 10 20 → coord mode") {
        if (ok("ui tap 10 20")) PASS();
        else FAIL("");
    }

    TEST("ui tap 100 200 → coord mode") {
        if (ok("ui tap 100 200")) PASS();
        else FAIL("");
    }

    TEST("ui hold 7 8 → coord mode") {
        if (ok("ui hold 7 8")) PASS();
        else FAIL("");
    }

    TEST("ui hold 7 8 1000 → coord + duration") {
        if (ok("ui hold 7 8 1000")) PASS();
        else FAIL("");
    }

    TEST("'120' alone → coord x, y missing → bounds error (NOT widget)") {
        auto r = R("ui tap 120");
        if (!r.success && r.errorMessage.find("not found") == std::string::npos) PASS();
        else FAIL("'120' must be coordinate, not widget search");
    }

    TEST("'0' alone → coord x=0, y missing → bounds error (NOT widget)") {
        auto r = R("ui tap 0");
        if (!r.success && r.errorMessage.find("not found") == std::string::npos) PASS();
        else FAIL("'0' must be coordinate, not widget search");
    }

    SECTION("13b. Auto-detect: widget IDs work");

    TEST("'btn7' → widget mode") {
        if (ok("ui tap btn7")) PASS();
        else FAIL("letters-first ID must go to widget mode");
    }

    TEST("'btnPlus' → widget mode") {
        if (ok("ui tap btnPlus")) PASS();
        else FAIL("");
    }

    TEST("'7btn' → widget mode (NOT coord 7)") {
        // Critical: "7btn" has a leading digit but is NOT a pure integer
        if (ok("ui tap 7btn")) PASS();
        else FAIL("'7btn' is not a pure number, must be widget mode");
    }

    TEST("'123abc' → widget mode (NOT coord 123)") {
        if (ok("ui tap 123abc")) PASS();
        else FAIL("'123abc' is not a pure number, must be widget mode");
    }

    TEST("'editInput' → widget mode") {
        if (ok("ui tap editInput")) PASS();
        else FAIL("");
    }

    TEST("'abc' → widget mode, not found → 'not found' error") {
        auto r = R("ui tap abc");
        if (!r.success && r.errorMessage.find("not found") != std::string::npos) PASS();
        else FAIL_V("expected 'not found', got '%s'", r.errorMessage.c_str());
    }

    SECTION("13c. Auto-detect: hold with widget + duration");

    TEST("ui hold btn7 → widget mode, default ms") {
        if (ok("ui hold btn7")) PASS();
        else FAIL(R("ui hold btn7").errorMessage.c_str());
    }

    TEST("ui hold btn7 1000 → widget mode, 1s") {
        if (ok("ui hold btn7 1000")) PASS();
        else FAIL("");
    }

    TEST("ui hold 7btn 500 → widget mode (NOT hold at x=7 y=btn)") {
        if (ok("ui hold 7btn 500")) PASS();
        else FAIL("'7btn' must be widget, not x-coordinate");
    }

    TEST("ui hold 123abc 2000 → widget mode") {
        if (ok("ui hold 123abc 2000")) PASS();
        else FAIL("");
    }

    SECTION("13d. Auto-detect: negative numbers");

    TEST("'-5' → argInt=-5 → widget mode → not found") {
        if (err("ui tap -5")) PASS();
        else FAIL("");
    }

    TEST("'-1 100' → x=-1 → widget mode → '-1' not found") {
        if (err("ui tap -1 100")) PASS();
        else FAIL("");
    }

    // ═══════════════════════════════════════════════════
    //  Summary
    // ═══════════════════════════════════════════════════

    printf("\n");
    printf("───────────────────────────────────────────────────\n");
    if (g_passed == g_total) {
        printf("  ✓ ALL %d TESTS PASSED\n", g_total);
        printf("───────────────────────────────────────────────────\n");
        return 0;
    } else {
        printf("  ✗ %d/%d PASSED, %d FAILED\n", g_passed, g_total, g_total - g_passed);
        printf("───────────────────────────────────────────────────\n");
        return 1;
    }
}
