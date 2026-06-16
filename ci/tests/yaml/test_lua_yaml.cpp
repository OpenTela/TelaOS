/**
 * Test: YAML Lua API
 *   YAML.load(filename)    — load from VFS
 *   YAML.loadText(text)    — parse from string
 *   yaml:get(path)         — dot-path access
 *   yaml:set(path, value)  — set + auto-create
 *   yaml:tree()            — direct table access
 *   yaml:save(filename?)   — write to VFS
 *   yaml:toText()          — serialize to string
 *   Auto-typing: numbers, booleans, strings
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
    printf("=== YAML Lua API Tests ===\n");
    setup();

    // ─── 1. loadText basic ──────────────────────────────

    SECTION("1. loadText + get");

    TEST("YAML.loadText returns object") {
        run(R"(
            local yaml = YAML.loadText("name: Alice\nage: 25\n")
            state.ok = yaml and "yes" or "no"
        )");
        if (get("ok") == "yes") PASS();
        else FAIL_V("ok='%s'", get("ok").c_str());
    }

    TEST("get simple string") {
        run(R"(
            local yaml = YAML.loadText("name: Alice\nage: 25\n")
            state.name = yaml:get("name")
        )");
        if (get("name") == "Alice") PASS();
        else FAIL_V("name='%s'", get("name").c_str());
    }

    TEST("get auto-typed number") {
        run(R"(
            local yaml = YAML.loadText("port: 8080\n")
            state.typ = type(yaml:get("port"))
            state.val = tostring(yaml:get("port"))
        )");
        if (get("typ") == "number" && get("val") == "8080") PASS();
        else FAIL_V("typ='%s' val='%s'", get("typ").c_str(), get("val").c_str());
    }

    TEST("get auto-typed boolean") {
        run(R"(
            local yaml = YAML.loadText("debug: true\nverbose: false\n")
            state.t = type(yaml:get("debug"))
            state.d = tostring(yaml:get("debug"))
            state.v = tostring(yaml:get("verbose"))
        )");
        if (get("t") == "boolean" && get("d") == "true" && get("v") == "false") PASS();
        else FAIL_V("t='%s' d='%s' v='%s'", get("t").c_str(), get("d").c_str(), get("v").c_str());
    }

    TEST("get nonexistent key returns nil") {
        run(R"(
            local yaml = YAML.loadText("a: 1\n")
            state.r = yaml:get("missing") == nil and "nil" or "not nil"
        )");
        if (get("r") == "nil") PASS();
        else FAIL_V("r='%s'", get("r").c_str());
    }

    // ─── 2. Nested maps ────────────────────────────────

    SECTION("2. Nested maps");

    TEST("get nested value with dot path") {
        run(R"(
            local yaml = YAML.loadText("server:\n  host: localhost\n  port: 8080\n")
            state.host = yaml:get("server.host")
            state.port = tostring(yaml:get("server.port"))
        )");
        if (get("host") == "localhost" && get("port") == "8080") PASS();
        else FAIL_V("host='%s' port='%s'", get("host").c_str(), get("port").c_str());
    }

    TEST("deeply nested (3 levels)") {
        run(R"(
            local yaml = YAML.loadText("a:\n  b:\n    c: deep\n")
            state.val = yaml:get("a.b.c")
        )");
        if (get("val") == "deep") PASS();
        else FAIL_V("val='%s'", get("val").c_str());
    }

    TEST("get nested missing returns nil") {
        run(R"(
            local yaml = YAML.loadText("server:\n  host: localhost\n")
            state.r = yaml:get("server.missing") == nil and "nil" or "not nil"
        )");
        if (get("r") == "nil") PASS();
        else FAIL_V("r='%s'", get("r").c_str());
    }

    // ─── 3. Arrays ─────────────────────────────────────

    SECTION("3. Arrays");

    TEST("simple array") {
        run(R"(
            local yaml = YAML.loadText("items:\n  - apple\n  - banana\n  - orange\n")
            local items = yaml:get("items")
            state.typ = type(items)
            state.len = tostring(#items)
            state.i1 = items[1]
            state.i3 = items[3]
        )");
        if (get("typ") == "table" && get("len") == "3" &&
            get("i1") == "apple" && get("i3") == "orange") PASS();
        else FAIL_V("typ='%s' len='%s' i1='%s'", get("typ").c_str(), get("len").c_str(), get("i1").c_str());
    }

    TEST("array of numbers") {
        run(R"(
            local yaml = YAML.loadText("nums:\n  - 10\n  - 20\n  - 30\n")
            local nums = yaml:get("nums")
            state.sum = tostring(nums[1] + nums[2] + nums[3])
        )");
        if (get("sum") == "60") PASS();
        else FAIL_V("sum='%s'", get("sum").c_str());
    }

    TEST("inline array [a, b, c]") {
        run(R"(
            local yaml = YAML.loadText("features: [api, websocket, admin]\n")
            local f = yaml:get("features")
            state.len = tostring(#f)
            state.f1 = f[1]
        )");
        if (get("len") == "3" && get("f1") == "api") PASS();
        else FAIL_V("len='%s' f1='%s'", get("len").c_str(), get("f1").c_str());
    }

    // ─── 4. set ────────────────────────────────────────

    SECTION("4. set");

    TEST("set simple value") {
        run(R"(
            local yaml = YAML.loadText("port: 8080\n")
            yaml:set("port", 9000)
            state.val = tostring(yaml:get("port"))
        )");
        if (get("val") == "9000") PASS();
        else FAIL_V("val='%s'", get("val").c_str());
    }

    TEST("set creates new key") {
        run(R"(
            local yaml = YAML.loadText("a: 1\n")
            yaml:set("b", 2)
            state.b = tostring(yaml:get("b"))
        )");
        if (get("b") == "2") PASS();
        else FAIL_V("b='%s'", get("b").c_str());
    }

    TEST("set nested path auto-creates parents") {
        run(R"(
            local yaml = YAML.loadText("a: 1\n")
            yaml:set("server.ssl.enabled", true)
            state.val = tostring(yaml:get("server.ssl.enabled"))
        )");
        if (get("val") == "true") PASS();
        else FAIL_V("val='%s'", get("val").c_str());
    }

    TEST("set array value") {
        run(R"(
            local yaml = YAML.loadText("x: 1\n")
            yaml:set("tags", {"lua", "esp32", "yaml"})
            local tags = yaml:get("tags")
            state.len = tostring(#tags)
            state.t1 = tags[1]
        )");
        if (get("len") == "3" && get("t1") == "lua") PASS();
        else FAIL_V("len='%s' t1='%s'", get("len").c_str(), get("t1").c_str());
    }

    // ─── 5. tree ───────────────────────────────────────

    SECTION("5. tree");

    TEST("tree returns table") {
        run(R"(
            local yaml = YAML.loadText("name: Alice\nage: 25\n")
            local t = yaml:tree()
            state.typ = type(t)
            state.name = t.name
        )");
        if (get("typ") == "table" && get("name") == "Alice") PASS();
        else FAIL_V("typ='%s' name='%s'", get("typ").c_str(), get("name").c_str());
    }

    TEST("tree nested access") {
        run(R"(
            local yaml = YAML.loadText("server:\n  host: localhost\n  port: 8080\n")
            local t = yaml:tree()
            state.host = t.server.host
            state.port = tostring(t.server.port)
        )");
        if (get("host") == "localhost" && get("port") == "8080") PASS();
        else FAIL_V("host='%s' port='%s'", get("host").c_str(), get("port").c_str());
    }

    TEST("tree modification reflects in get") {
        run(R"(
            local yaml = YAML.loadText("value: old\n")
            local t = yaml:tree()
            t.value = "new"
            state.val = yaml:get("value")
        )");
        if (get("val") == "new") PASS();
        else FAIL_V("val='%s'", get("val").c_str());
    }

    TEST("tree is reference, not copy") {
        run(R"(
            local yaml = YAML.loadText("x: 1\n")
            local t1 = yaml:tree()
            local t2 = yaml:tree()
            t1.x = 99
            state.val = tostring(t2.x)
        )");
        if (get("val") == "99") PASS();
        else FAIL_V("val='%s'", get("val").c_str());
    }

    // ─── 6. toText ─────────────────────────────────────

    SECTION("6. toText");

    TEST("serializes to YAML string") {
        run(R"(
            local yaml = YAML.loadText("name: Alice\nage: 25\n")
            local text = yaml:toText()
            state.hasName = text:find("name:") and "yes" or "no"
            state.hasAge = text:find("age: 25") and "yes" or "no"
        )");
        if (get("hasName") == "yes" && get("hasAge") == "yes") PASS();
        else FAIL_V("hasName='%s' hasAge='%s'", get("hasName").c_str(), get("hasAge").c_str());
    }

    TEST("toText preserves types") {
        run(R"(
            local yaml = YAML.loadText("flag: true\ncount: 42\nname: Bob\n")
            local text = yaml:toText()
            state.hasTrue = text:find("flag: true") and "yes" or "no"
            state.has42 = text:find("count: 42") and "yes" or "no"
        )");
        if (get("hasTrue") == "yes" && get("has42") == "yes") PASS();
        else FAIL_V("hasTrue='%s' has42='%s'", get("hasTrue").c_str(), get("has42").c_str());
    }

    // ─── 7. VFS: load + save ───────────────────────────

    SECTION("7. VFS file I/O");

    TEST("YAML.load reads from VFS") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();
        LittleFS.writeFile("/data/config.yaml", "server:\n  host: localhost\n  port: 8080\ndebug: true\n");

        run(R"(
            local yaml = YAML.load("/data/config.yaml")
            if yaml then
                state.host = yaml:get("server.host")
                state.port = tostring(yaml:get("server.port"))
                state.debug = tostring(yaml:get("debug"))
            else
                state.host = "nil"
            end
        )");
        if (get("host") == "localhost" && get("port") == "8080" && get("debug") == "true") PASS();
        else FAIL_V("host='%s' port='%s' debug='%s'", get("host").c_str(), get("port").c_str(), get("debug").c_str());
    }

    TEST("YAML.load returns nil for missing file") {
        LittleFS.reset();
        LittleFS.begin();

        run(R"(
            local yaml, err = YAML.load("/data/nope.yaml")
            state.isNil = yaml == nil and "yes" or "no"
            state.hasErr = err and "yes" or "no"
        )");
        if (get("isNil") == "yes" && get("hasErr") == "yes") PASS();
        else FAIL_V("isNil='%s' hasErr='%s'", get("isNil").c_str(), get("hasErr").c_str());
    }

    TEST("yaml:save() writes to VFS") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();

        run(R"(
            local yaml = YAML.loadText("theme: light\nfontSize: 14\n")
            yaml:set("theme", "dark")
            yaml:set("fontSize", 18)
            local ok = yaml:save("/data/settings.yaml")
            state.ok = ok and "yes" or "no"
        )");

        bool inVfs = LittleFS.fileExists("/data/settings.yaml");
        std::string raw = LittleFS.readFile("/data/settings.yaml");

        if (get("ok") == "yes" && inVfs &&
            raw.find("dark") != std::string::npos &&
            raw.find("18") != std::string::npos) PASS();
        else FAIL_V("ok='%s' inVfs=%d raw='%.60s'", get("ok").c_str(), inVfs, raw.c_str());
    }

    TEST("save then load roundtrip through VFS") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();

        run(R"(
            local yaml = YAML.loadText("x: 1\ny: 2\n")
            yaml:set("x", 100)
            yaml:set("z", "new")
            yaml:save("/data/rt.yaml")

            local yaml2 = YAML.load("/data/rt.yaml")
            state.x = tostring(yaml2:get("x"))
            state.y = tostring(yaml2:get("y"))
            state.z = yaml2:get("z")
        )");
        if (get("x") == "100" && get("y") == "2" && get("z") == "new") PASS();
        else FAIL_V("x='%s' y='%s' z='%s'", get("x").c_str(), get("y").c_str(), get("z").c_str());
    }

    TEST("save to original filename") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        LittleFS.begin();
        LittleFS.writeFile("/data/orig.yaml", "val: old\n");

        run(R"(
            local yaml = YAML.load("/data/orig.yaml")
            yaml:set("val", "new")
            yaml:save()

            local yaml2 = YAML.load("/data/orig.yaml")
            state.val = yaml2:get("val")
        )");
        if (get("val") == "new") PASS();
        else FAIL_V("val='%s'", get("val").c_str());
    }

    // ─── 8. Quoted strings ─────────────────────────────

    SECTION("8. Quoted strings / special values");

    TEST("quoted number stays string") {
        run(R"(
            local yaml = YAML.loadText('port: "8080"\n')
            state.typ = type(yaml:get("port"))
            state.val = yaml:get("port")
        )");
        if (get("typ") == "string" && get("val") == "8080") PASS();
        else FAIL_V("typ='%s' val='%s'", get("typ").c_str(), get("val").c_str());
    }

    TEST("null/~ becomes nil") {
        run(R"(
            local yaml = YAML.loadText("a: null\nb: ~\n")
            state.a = yaml:get("a") == nil and "nil" or "not nil"
            state.b = yaml:get("b") == nil and "nil" or "not nil"
        )");
        if (get("a") == "nil" && get("b") == "nil") PASS();
        else FAIL_V("a='%s' b='%s'", get("a").c_str(), get("b").c_str());
    }

    TEST("yes/no as booleans") {
        run(R"(
            local yaml = YAML.loadText("a: yes\nb: no\n")
            state.a = tostring(yaml:get("a"))
            state.b = tostring(yaml:get("b"))
        )");
        if (get("a") == "true" && get("b") == "false") PASS();
        else FAIL_V("a='%s' b='%s'", get("a").c_str(), get("b").c_str());
    }

    TEST("float number") {
        run(R"(
            local yaml = YAML.loadText("pi: 3.14\n")
            state.typ = type(yaml:get("pi"))
            state.approx = tostring(yaml:get("pi") > 3.1 and yaml:get("pi") < 3.2)
        )");
        if (get("typ") == "number" && get("approx") == "true") PASS();
        else FAIL_V("typ='%s' approx='%s'", get("typ").c_str(), get("approx").c_str());
    }

    // ─── 9. Comments ───────────────────────────────────

    SECTION("9. Comments");

    TEST("inline comments stripped") {
        run(R"(
            local yaml = YAML.loadText("port: 8080  # default port\nhost: localhost # server\n")
            state.port = tostring(yaml:get("port"))
            state.host = yaml:get("host")
        )");
        if (get("port") == "8080" && get("host") == "localhost") PASS();
        else FAIL_V("port='%s' host='%s'", get("port").c_str(), get("host").c_str());
    }

    TEST("full-line comments skipped") {
        run(R"(
            local yaml = YAML.loadText("# comment\nname: Alice\n# another\nage: 25\n")
            state.name = yaml:get("name")
            state.age = tostring(yaml:get("age"))
        )");
        if (get("name") == "Alice" && get("age") == "25") PASS();
        else FAIL_V("name='%s' age='%s'", get("name").c_str(), get("age").c_str());
    }

    // ─── 10. Complex structures ────────────────────────

    SECTION("10. Complex structures");

    TEST("multi-section config") {
        run(R"(
            local yaml = YAML.loadText([[
server:
  host: localhost
  port: 8080
database:
  name: mydb
  pool: 10
debug: true
]])
            state.host = yaml:get("server.host")
            state.port = tostring(yaml:get("server.port"))
            state.db = yaml:get("database.name")
            state.pool = tostring(yaml:get("database.pool"))
            state.debug = tostring(yaml:get("debug"))
        )");
        if (get("host") == "localhost" && get("port") == "8080" &&
            get("db") == "mydb" && get("pool") == "10" && get("debug") == "true") PASS();
        else FAIL_V("host='%s' port='%s' db='%s'", get("host").c_str(), get("port").c_str(), get("db").c_str());
    }

    TEST("tree iteration") {
        run(R"(
            local yaml = YAML.loadText("a: 1\nb: 2\nc: 3\n")
            local t = yaml:tree()
            local sum = 0
            for k, v in pairs(t) do
                sum = sum + v
            end
            state.sum = tostring(sum)
        )");
        if (get("sum") == "6") PASS();
        else FAIL_V("sum='%s'", get("sum").c_str());
    }

    TEST("set then toText reflects changes") {
        run(R"(
            local yaml = YAML.loadText("x: 1\n")
            yaml:set("x", 99)
            yaml:set("y", "hello")
            local text = yaml:toText()
            state.hasX = text:find("x: 99") and "yes" or "no"
            state.hasY = text:find("y: hello") and "yes" or "no"
        )");
        if (get("hasX") == "yes" && get("hasY") == "yes") PASS();
        else FAIL_V("hasX='%s' hasY='%s'", get("hasX").c_str(), get("hasY").c_str());
    }

    // ─── 11. Error handling ────────────────────────────

    SECTION("11. Error handling");

    TEST("loadText empty string → nil + error") {
        run(R"(
            local yaml, err = YAML.loadText("")
            state.isNil = yaml == nil and "yes" or "no"
        )");
        if (get("isNil") == "yes") PASS();
        else FAIL_V("isNil='%s'", get("isNil").c_str());
    }

    TEST("save without filename → error") {
        run(R"(
            local yaml = YAML.loadText("a: 1\n")
            local ok, err = yaml:save()
            state.ok = tostring(ok)
            state.hasErr = err and "yes" or "no"
        )");
        if (get("ok") == "false" && get("hasErr") == "yes") PASS();
        else FAIL_V("ok='%s' hasErr='%s'", get("ok").c_str(), get("hasErr").c_str());
    }

    // ═════════════════════════════════════════════════════

    printf("\n────────────────────────────────────────────────────\n");
    if (g_passed == g_total) {
        printf("  ✓ ALL %d YAML LUA API TESTS PASSED\n", g_total);
    } else {
        printf("  ✗ %d/%d PASSED, %d FAILED\n", g_passed, g_total, g_total - g_passed);
    }
    printf("────────────────────────────────────────────────────\n");
    return g_passed == g_total ? 0 : 1;
}
