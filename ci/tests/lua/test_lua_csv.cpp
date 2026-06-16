/**
 * Test: CSV Lua API
 *   CSV.loadText(text)   — parse from string
 *   csv:records(count?)  — dicts
 *   csv:rows(count?)     — arrays
 *   csv:rawText()        — serialize
 *   csv:add(record)      — dict or array format
 *   Escape: semicolons, quotes, roundtrip
 *   Error handling: empty, mismatch
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "lvgl.h"
#include "lvgl_mock.h"
#include <LittleFS.h>
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"
#include "ui/ui_html.h"

static int g_passed = 0, g_total = 0;
#define TEST(name)     printf("  %-55s ", name); g_total++;
#define PASS()         do { printf("✓\n"); g_passed++; } while(0)
#define FAIL(msg)      printf("✗ %s\n", msg)
#define FAIL_V(f, ...) do { printf("✗ "); printf(f, __VA_ARGS__); printf("\n"); } while(0)
#define SECTION(name)  printf("\n%s:\n", name)

static LuaEngine g_lua_engine;

static std::string get(const char* var) {
    return g_core.store().getString(var);
}

static bool run(const char* code) {
    return g_lua_engine.execute(code);
}

static void setup() {
    LvglMock::create_screen(240, 240);
    g_core.store().clear();
    g_lua_engine.shutdown();
    g_lua_engine.init();
    g_core.initDynamicApp(nullptr);
}

int main() {
    printf("=== CSV Lua API Tests ===\n");
    setup();

    // ─── 1. CSV.loadText basic ──────────────────────────

    SECTION("1. loadText + records");

    TEST("CSV.loadText returns object") {
        bool ok = run(R"(
            local csv = CSV.loadText("name;email;age\nAlice;alice@ex.com;25\nBob;bob@ex.com;30\n")
            state.ok = csv and "yes" or "no"
        )");
        if (ok && get("ok") == "yes") PASS();
        else FAIL_V("ok='%s'", get("ok").c_str());
    }

    TEST("records() returns all 2 records") {
        run(R"(
            local csv = CSV.loadText("name;age\nAlice;25\nBob;30\n")
            local r = csv:records()
            state.count = tostring(#r)
        )");
        if (get("count") == "2") PASS();
        else FAIL_V("count='%s'", get("count").c_str());
    }

    TEST("records() fields accessible by header name") {
        run(R"(
            local csv = CSV.loadText("name;age\nAlice;25\nBob;30\n")
            local r = csv:records()
            state.name1 = r[1].name
            state.age1 = r[1].age
            state.name2 = r[2].name
        )");
        if (get("name1") == "Alice" && get("age1") == "25" && get("name2") == "Bob") PASS();
        else FAIL_V("name1='%s' age1='%s' name2='%s'", get("name1").c_str(), get("age1").c_str(), get("name2").c_str());
    }

    TEST("records(1) returns last 1 record") {
        run(R"(
            local csv = CSV.loadText("x\nA\nB\nC\n")
            local r = csv:records(1)
            state.count = tostring(#r)
            state.val = r[1].x
        )");
        if (get("count") == "1" && get("val") == "C") PASS();
        else FAIL_V("count='%s' val='%s'", get("count").c_str(), get("val").c_str());
    }

    // ─── 2. rows() ──────────────────────────────────────

    SECTION("2. rows()");

    TEST("rows() returns arrays") {
        run(R"(
            local csv = CSV.loadText("a;b\n1;2\n3;4\n")
            local r = csv:rows()
            state.r1a = r[1][1]
            state.r1b = r[1][2]
            state.r2a = r[2][1]
        )");
        if (get("r1a") == "1" && get("r1b") == "2" && get("r2a") == "3") PASS();
        else FAIL_V("r1a='%s' r1b='%s' r2a='%s'", get("r1a").c_str(), get("r1b").c_str(), get("r2a").c_str());
    }

    TEST("rows(2) returns last 2") {
        run(R"(
            local csv = CSV.loadText("x\nA\nB\nC\nD\n")
            local r = csv:rows(2)
            state.count = tostring(#r)
            state.first = r[1][1]
            state.second = r[2][1]
        )");
        if (get("count") == "2" && get("first") == "C" && get("second") == "D") PASS();
        else FAIL_V("count='%s' first='%s' second='%s'", get("count").c_str(), get("first").c_str(), get("second").c_str());
    }

    // ─── 3. add() ───────────────────────────────────────

    SECTION("3. add()");

    TEST("add dict format") {
        run(R"(
            local csv = CSV.loadText("name;score\nAlice;100\n")
            csv:add({name="Bob", score="200"})
            local r = csv:records()
            state.count = tostring(#r)
            state.newName = r[2].name
            state.newScore = r[2].score
        )");
        if (get("count") == "2" && get("newName") == "Bob" && get("newScore") == "200") PASS();
        else FAIL_V("count='%s' name='%s' score='%s'", get("count").c_str(), get("newName").c_str(), get("newScore").c_str());
    }

    TEST("add array format") {
        run(R"(
            local csv = CSV.loadText("a;b\n1;2\n")
            csv:add({"3", "4"})
            local r = csv:rows()
            state.count = tostring(#r)
            state.val = r[2][1]
        )");
        if (get("count") == "2" && get("val") == "3") PASS();
        else FAIL_V("count='%s' val='%s'", get("count").c_str(), get("val").c_str());
    }

    TEST("add multiple then records") {
        run(R"(
            local csv = CSV.loadText("x\n")
            csv:add({x="A"})
            csv:add({x="B"})
            csv:add({x="C"})
            local r = csv:records()
            state.count = tostring(#r)
            state.last = r[3].x
        )");
        if (get("count") == "3" && get("last") == "C") PASS();
        else FAIL_V("count='%s' last='%s'", get("count").c_str(), get("last").c_str());
    }

    // ─── 4. rawText() ───────────────────────────────────

    SECTION("4. rawText()");

    TEST("rawText serializes correctly") {
        run(R"(
            local csv = CSV.loadText("name;age\nAlice;25\n")
            csv:add({name="Bob", age="30"})
            state.text = csv:rawText()
        )");
        std::string text = get("text");
        // Should contain header + 2 rows
        if (text.find("name;age") != std::string::npos &&
            text.find("Alice;25") != std::string::npos &&
            text.find("Bob;30") != std::string::npos) PASS();
        else FAIL_V("text='%s'", text.c_str());
    }

    // ─── 5. Escape ──────────────────────────────────────

    SECTION("5. Escape");

    TEST("semicolon in field is escaped") {
        run(R"(
            local csv = CSV.loadText("text\n")
            csv:add({text="Hello; World"})
            state.raw = csv:rawText()
        )");
        std::string raw = get("raw");
        // Should be quoted: "Hello; World"
        if (raw.find("\"Hello; World\"") != std::string::npos) PASS();
        else FAIL_V("raw='%s'", raw.c_str());
    }

    TEST("quotes in field are escaped") {
        run(R"(
            local csv = CSV.loadText("text\n")
            csv:add({text='Say "Hello"'})
            state.raw = csv:rawText()
        )");
        std::string raw = get("raw");
        // Should have doubled quotes
        if (raw.find("\"\"Hello\"\"") != std::string::npos) PASS();
        else FAIL_V("raw='%s'", raw.c_str());
    }

    TEST("escape roundtrip — serialize then parse back") {
        run(R"(
            local csv = CSV.loadText("text\n")
            csv:add({text="Hello; World"})
            csv:add({text='Say "Hi"'})
            local raw = csv:rawText()
            local csv2 = CSV.loadText(raw)
            local r = csv2:records()
            state.v1 = r[1].text
            state.v2 = r[2].text
        )");
        if (get("v1") == "Hello; World" && get("v2") == "Say \"Hi\"") PASS();
        else FAIL_V("v1='%s' v2='%s'", get("v1").c_str(), get("v2").c_str());
    }

    // ─── 6. Error handling ──────────────────────────────

    SECTION("6. Error handling");

    TEST("empty text returns nil + error") {
        run(R"(
            local csv, err = CSV.loadText("")
            state.isNil = csv == nil and "yes" or "no"
            state.hasErr = err and "yes" or "no"
        )");
        if (get("isNil") == "yes" && get("hasErr") == "yes") PASS();
        else FAIL_V("isNil='%s' hasErr='%s'", get("isNil").c_str(), get("hasErr").c_str());
    }

    TEST("add array with wrong field count → error") {
        run(R"(
            local csv = CSV.loadText("a;b;c\n1;2;3\n")
            local ok, err = pcall(function()
                csv:add({"too", "many", "fields", "here"})
            end)
            state.ok = ok and "yes" or "no"
            state.hasErr = err and "yes" or "no"
        )");
        if (get("ok") == "no" && get("hasErr") == "yes") PASS();
        else FAIL_V("ok='%s' hasErr='%s'", get("ok").c_str(), get("hasErr").c_str());
    }

    // ─── 7. Practical usage ─────────────────────────────

    SECTION("7. Practical usage");

    TEST("log-like pattern: create + add + read last") {
        run(R"(
            local csv = CSV.loadText("time;event\n")
            csv:add({time="10:00", event="start"})
            csv:add({time="10:05", event="click"})
            csv:add({time="10:10", event="save"})
            csv:add({time="10:15", event="exit"})
            local last2 = csv:records(2)
            state.count = tostring(#last2)
            state.ev1 = last2[1].event
            state.ev2 = last2[2].event
        )");
        if (get("count") == "2" && get("ev1") == "save" && get("ev2") == "exit") PASS();
        else FAIL_V("count='%s' ev1='%s' ev2='%s'", get("count").c_str(), get("ev1").c_str(), get("ev2").c_str());
    }

    TEST("3-column data with numeric strings") {
        run(R"(
            local csv = CSV.loadText("name;amount;date\nAlice;100;2026-01\nBob;200;2026-02\n")
            local r = csv:records()
            local sum = 0
            for _, rec in ipairs(r) do
                sum = sum + tonumber(rec.amount)
            end
            state.sum = tostring(sum)
        )");
        if (get("sum") == "300") PASS();
        else FAIL_V("sum='%s'", get("sum").c_str());
    }

    // ─── 8. VFS file I/O (CSV.load + csv:save) ─────────

    SECTION("8. VFS file I/O");

    TEST("CSV.load reads from VFS") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();
        LittleFS.writeFile("/data/users.csv", "name;age\nAlice;25\nBob;30\n");

        run(R"(
            local csv = CSV.load("/data/users.csv")
            if csv then
                local r = csv:records()
                state.count = tostring(#r)
                state.name1 = r[1].name
                state.age2 = r[2].age
            else
                state.count = "nil"
            end
        )");
        if (get("count") == "2" && get("name1") == "Alice" && get("age2") == "30") PASS();
        else FAIL_V("count='%s' name1='%s' age2='%s'", get("count").c_str(), get("name1").c_str(), get("age2").c_str());
    }

    TEST("CSV.load returns nil for missing file") {
        LittleFS.reset();
        LittleFS.begin();

        run(R"(
            local csv, err = CSV.load("/data/nope.csv")
            state.isNil = csv == nil and "yes" or "no"
            state.hasErr = err and "yes" or "no"
        )");
        if (get("isNil") == "yes" && get("hasErr") == "yes") PASS();
        else FAIL_V("isNil='%s' hasErr='%s'", get("isNil").c_str(), get("hasErr").c_str());
    }

    TEST("csv:save() writes to VFS") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();
        LittleFS.writeFile("/data/log.csv", "event;time\n");

        run(R"(
            local csv = CSV.load("/data/log.csv")
            csv:add({event="start", time="10:00"})
            csv:add({event="stop", time="10:05"})
            local ok = csv:save()
            state.ok = ok and "yes" or "no"
        )");

        // Verify file in VFS
        bool inVfs = LittleFS.fileExists("/data/log.csv");
        std::string raw = LittleFS.readFile("/data/log.csv");

        if (get("ok") == "yes" && inVfs &&
            raw.find("start;10:00") != std::string::npos &&
            raw.find("stop;10:05") != std::string::npos) PASS();
        else FAIL_V("ok='%s' inVfs=%d raw='%.60s'", get("ok").c_str(), inVfs, raw.c_str());
    }

    TEST("csv:save(true) appends only new rows to VFS") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();
        LittleFS.writeFile("/data/scores.csv", "name;score\nAlice;100\n");

        run(R"(
            local csv = CSV.load("/data/scores.csv")
            csv:add({name="Bob", score="200"})
            local ok = csv:save(true)
            state.ok = ok and "yes" or "no"
        )");

        std::string raw = LittleFS.readFile("/data/scores.csv");

        // Should have original + appended
        if (get("ok") == "yes" &&
            raw.find("Alice;100") != std::string::npos &&
            raw.find("Bob;200") != std::string::npos) PASS();
        else FAIL_V("ok='%s' raw='%.80s'", get("ok").c_str(), raw.c_str());
    }

    TEST("save then load roundtrip through VFS") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();
        LittleFS.writeFile("/data/rt.csv", "key;val\n");

        run(R"(
            local csv = CSV.load("/data/rt.csv")
            csv:add({key="color", val="red"})
            csv:add({key="size", val="42"})
            csv:save()

            -- Load again from VFS
            local csv2 = CSV.load("/data/rt.csv")
            local r = csv2:records()
            state.count = tostring(#r)
            state.k1 = r[1].key
            state.v2 = r[2].val
        )");
        if (get("count") == "2" && get("k1") == "color" && get("v2") == "42") PASS();
        else FAIL_V("count='%s' k1='%s' v2='%s'", get("count").c_str(), get("k1").c_str(), get("v2").c_str());
    }

    // ═════════════════════════════════════════════════════

    g_lua_engine.shutdown();

    printf("\n────────────────────────────────────────────────────\n");
    if (g_passed == g_total) {
        printf("  ✓ ALL %d CSV LUA API TESTS PASSED\n", g_total);
    } else {
        printf("  ✗ %d/%d PASSED, %d FAILED\n", g_passed, g_total, g_total - g_passed);
    }
    printf("────────────────────────────────────────────────────\n");
    return g_passed == g_total ? 0 : 1;
}
