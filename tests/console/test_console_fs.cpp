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
#include "ui/ui_engine.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static void setup() {
    LvglMock::create_screen(240, 240);
    State::store().clear();
    UI::Engine::instance().init();
}

int main() {
    printf("=== Console FS Tests ===\n\n");
    int passed = 0, total = 0;

    setup();

    // === app pull (single file) ===
    printf("app pull:\n");

    TEST("pull existing app returns ok + binary") {
        VFS::reset();
        VFS::mkdir("/apps");
        VFS::mkdir("/apps/calc");
        VFS::writeFile("/apps/calc/calc.bax", "<app><ui default=\"/m\"><page id=\"m\"><label>Hi</label></page></ui></app>");

        auto r = Console::exec("app pull calc");
        if (r.success && r.payload && r.payloadSize > 0) PASS();
        else { FAIL(""); printf("      ok=%d payload=%p size=%u\n",
            r.success, r.payload, r.payloadSize); }
        if (r.payload) free(r.payload);
    }

    TEST("pull returns correct file content") {
        VFS::reset();
        VFS::mkdir("/apps");
        VFS::mkdir("/apps/test");
        const char* content = "Hello World App";
        VFS::writeFile("/apps/test/test.bax", content);

        auto r = Console::exec("app pull test");
        bool match = r.payload && r.payloadSize == strlen(content) &&
                     memcmp(r.payload, content, r.payloadSize) == 0;
        if (match) PASS();
        else { FAIL(""); printf("      size=%u expected=%zu\n", r.payloadSize, strlen(content)); }
        if (r.payload) free(r.payload);
    }

    TEST("pull missing app returns error") {
        VFS::reset();
        auto r = Console::exec("app pull nonexistent");
        if (!r.success) PASS(); else FAIL("should fail");
    }

    TEST("pull without name returns error") {
        VFS::reset();
        auto r = Console::exec("app pull");
        if (!r.success) PASS(); else FAIL("should fail");
    }

    // === app pull * (all files) ===
    printf("\napp pull *:\n");

    TEST("pull * returns all files concatenated") {
        VFS::reset();
        VFS::mkdir("/apps");
        VFS::mkdir("/apps/myapp");
        VFS::writeFile("/apps/myapp/myapp.bax", "APPHTML");
        VFS::writeFile("/apps/myapp/icon.png", "PNGDATA");

        auto r = Console::exec("app pull myapp *");
        if (r.success && r.payload && r.payloadSize == 14) PASS(); // 7 + 7
        else { FAIL(""); printf("      ok=%d size=%u\n", r.success, r.payloadSize); }
        if (r.payload) free(r.payload);
    }

    TEST("pull * with resources/ subfolder") {
        VFS::reset();
        VFS::mkdir("/apps");
        VFS::mkdir("/apps/game");
        VFS::mkdir("/apps/game/resources");
        VFS::writeFile("/apps/game/game.bax", "HTML");
        VFS::writeFile("/apps/game/resources/sprite.png", "IMG");

        auto r = Console::exec("app pull game *");
        if (r.success && r.payloadSize == 7) PASS(); // 4 + 3
        else { FAIL(""); printf("      ok=%d size=%u\n", r.success, r.payloadSize); }
        if (r.payload) free(r.payload);
    }

    // === app delete ===
    printf("\napp delete:\n");

    TEST("delete removes app directory and files") {
        VFS::reset();
        VFS::mkdir("/apps");
        VFS::mkdir("/apps/old");
        VFS::writeFile("/apps/old/old.bax", "data");
        VFS::writeFile("/apps/old/icon.png", "icon");

        auto r = Console::exec("app delete old");
        if (r.success) {
            // Files should be removed
            bool noFile = !VFS::exists("/apps/old/old.bax");
            bool noIcon = !VFS::exists("/apps/old/icon.png");
            bool noDir = !VFS::exists("/apps/old");
            if (noFile && noIcon && noDir) PASS();
            else { FAIL(""); printf("      file=%d icon=%d dir=%d\n", !noFile, !noIcon, !noDir); }
        } else FAIL("not success");
    }

    TEST("delete without name returns error") {
        VFS::reset();
        auto r = Console::exec("app delete");
        if (!r.success) PASS(); else FAIL("should fail");
    }

    TEST("delete missing app — no crash") {
        VFS::reset();
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
