/**
 * Test: Console FS Commands
 * app pull, app delete — using VFS for file operations
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <LittleFS.h>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "console/console.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static void setup() {
    LvglMock::create_screen(240, 240);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
}

int main() {
    printf("=== Console FS Tests ===\n\n");
    int passed = 0, total = 0;

    setup();

    // === app pull (single file) ===
    printf("app pull:\n");

    TEST("pull existing app returns ok + binary") {
        LittleFS.reset();
        LittleFS.mkdir("/apps");
        LittleFS.mkdir("/apps/calc");
        LittleFS.writeFile("/apps/calc/calc.bax", "<app><ui default=\"/m\"><page id=\"m\"><label>Hi</label></page></ui></app>");

        auto r = Console::exec("app pull calc");
        if (r.success && r.payload && r.payloadSize > 0) PASS();
        else { FAIL(""); printf("      ok=%d payload=%p size=%u\n",
            r.success, r.payload, r.payloadSize); }
        if (r.payload) free(r.payload);
    }

    TEST("pull returns correct file content") {
        LittleFS.reset();
        LittleFS.mkdir("/apps");
        LittleFS.mkdir("/apps/test");
        const char* content = "Hello World App";
        LittleFS.writeFile("/apps/test/test.bax", content);

        auto r = Console::exec("app pull test");
        bool match = r.payload && r.payloadSize == strlen(content) &&
                     memcmp(r.payload, content, r.payloadSize) == 0;
        if (match) PASS();
        else { FAIL(""); printf("      size=%u expected=%zu\n", r.payloadSize, strlen(content)); }
        if (r.payload) free(r.payload);
    }

    TEST("pull missing app returns error") {
        LittleFS.reset();
        auto r = Console::exec("app pull nonexistent");
        if (!r.success) PASS(); else FAIL("should fail");
    }

    TEST("pull without name returns error") {
        LittleFS.reset();
        auto r = Console::exec("app pull");
        if (!r.success) PASS(); else FAIL("should fail");
    }

    // === app pull * (all files) ===
    printf("\napp pull *:\n");

    TEST("pull * returns all files concatenated") {
        LittleFS.reset();
        LittleFS.mkdir("/apps");
        LittleFS.mkdir("/apps/myapp");
        LittleFS.writeFile("/apps/myapp/myapp.bax", "APPHTML");
        LittleFS.writeFile("/apps/myapp/icon.png", "PNGDATA");

        auto r = Console::exec("app pull myapp *");
        if (r.success && r.payload && r.payloadSize == 14) PASS(); // 7 + 7
        else { FAIL(""); printf("      ok=%d size=%u\n", r.success, r.payloadSize); }
        if (r.payload) free(r.payload);
    }

    TEST("pull * with resources/ subfolder") {
        LittleFS.reset();
        LittleFS.mkdir("/apps");
        LittleFS.mkdir("/apps/game");
        LittleFS.mkdir("/apps/game/resources");
        LittleFS.writeFile("/apps/game/game.bax", "HTML");
        LittleFS.writeFile("/apps/game/resources/sprite.png", "IMG");

        auto r = Console::exec("app pull game *");
        if (r.success && r.payloadSize == 7) PASS(); // 4 + 3
        else { FAIL(""); printf("      ok=%d size=%u\n", r.success, r.payloadSize); }
        if (r.payload) free(r.payload);
    }

    // === app delete ===
    printf("\napp delete:\n");

    TEST("delete removes app directory and files") {
        LittleFS.reset();
        LittleFS.mkdir("/apps");
        LittleFS.mkdir("/apps/old");
        LittleFS.writeFile("/apps/old/old.bax", "data");
        LittleFS.writeFile("/apps/old/icon.png", "icon");

        auto r = Console::exec("app delete old");
        if (r.success) {
            // Files should be removed
            bool noFile = !LittleFS.exists("/apps/old/old.bax");
            bool noIcon = !LittleFS.exists("/apps/old/icon.png");
            bool noDir = !LittleFS.exists("/apps/old");
            if (noFile && noIcon && noDir) PASS();
            else { FAIL(""); printf("      file=%d icon=%d dir=%d\n", !noFile, !noIcon, !noDir); }
        } else FAIL("not success");
    }

    TEST("delete without name returns error") {
        LittleFS.reset();
        auto r = Console::exec("app delete");
        if (!r.success) PASS(); else FAIL("should fail");
    }

    TEST("delete missing app — no crash") {
        LittleFS.reset();
        auto r = Console::exec("app delete ghost");
        // Should succeed (nothing to delete)
        PASS();
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CONSOLE FS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d CONSOLE FS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
