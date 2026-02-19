/**
 * Test: App Manager
 * scanApps from VFS, extractMeta, launch, systemConfig, pending queue
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <LittleFS.h>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/app_manager.h"
#include "core/state_store.h"
#include "ui/ui_engine.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static App::Manager& mgr() { return App::Manager::instance(); }

// Find app by name in manager's list
static const App::AppInfo* findApp(const char* name) {
    for (auto& app : mgr().apps()) {
        if (app.name == name) return &app;
    }
    return nullptr;
}

static void setupVFS() {
    VFS::reset();
    State::store().clear();

    // Create /apps/ directory
    VFS::mkdir("/apps");

    // App 1: calculator with metadata
    VFS::mkdir("/apps/calc");
    VFS::writeFile("/apps/calc/calc.bax",
        "<app title=\"Calculator\" icon=\"system:calculator\" category=\"tool\" version=\"1.0\">\n"
        "  <ui default=\"/main\">\n"
        "    <page id=\"main\"><label>Calc</label></page>\n"
        "  </ui>\n"
        "</app>\n"
    );

    // App 2: simple app without metadata
    VFS::mkdir("/apps/hello");
    VFS::writeFile("/apps/hello/hello.bax",
        "<app>\n"
        "  <ui default=\"/main\">\n"
        "    <page id=\"main\"><label>Hello</label></page>\n"
        "  </ui>\n"
        "</app>\n"
    );

    // App 3: app with icon.png
    VFS::mkdir("/apps/game");
    VFS::writeFile("/apps/game/game.bax",
        "<app title=\"My Game\" category=\"game\">\n"
        "  <ui default=\"/main\">\n"
        "    <page id=\"main\"><label>Game</label></page>\n"
        "  </ui>\n"
        "</app>\n"
    );
    VFS::writeFile("/apps/game/icon.png", "PNGDATA");

    // System config directory
    VFS::mkdir("/system");
}

int main() {
    printf("=== App Manager Tests ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(480, 480);
    UI::Engine::instance().init();

    // === AppInfo::path() ===
    printf("AppInfo:\n");

    TEST("path() constructs correct path") {
        App::AppInfo info;
        info.name = "calc";
        P::String p = info.path();
        if (p == "/apps/calc/calc.bax") PASS();
        else FAIL(p.c_str());
    }

    TEST("path() with different name") {
        App::AppInfo info;
        info.name = "myapp";
        P::String p = info.path();
        if (p == "/apps/myapp/myapp.bax") PASS();
        else FAIL(p.c_str());
    }

    // === scanApps via init() ===
    printf("\nscanApps (via init):\n");

    setupVFS();
    mgr().init();

    TEST("finds apps from VFS") {
        auto& apps = mgr().apps();
        // Should find at least our 3 HTML apps (may include native apps too)
        int htmlCount = 0;
        for (auto& a : apps) {
            if (a.source == App::AppInfo::HTML) htmlCount++;
        }
        if (htmlCount >= 3) PASS();
        else { FAIL(""); printf("      html apps: %d, total: %zu\n", htmlCount, apps.size()); }
    }

    TEST("calc app found with correct title") {
        auto* app = findApp("calc");
        if (app && app->title == "Calculator") PASS();
        else { FAIL(""); if (app) printf("      title='%s'\n", app->title.c_str()); }
    }

    TEST("calc app has category") {
        auto* app = findApp("calc");
        if (app && app->category == "tool") PASS();
        else { FAIL(""); if (app) printf("      cat='%s'\n", app->category.c_str()); }
    }

    TEST("calc app has system icon path") {
        auto* app = findApp("calc");
        if (app && !app->iconPath.empty()) {
            // Should resolve system:calculator to C:/system/resources/icons/calculator.png
            bool hasSysIcon = app->iconPath.find("calculator") != P::String::npos;
            if (hasSysIcon) PASS();
            else { FAIL(""); printf("      icon='%s'\n", app->iconPath.c_str()); }
        } else { FAIL("no icon path"); }
    }

    TEST("hello app defaults title to capitalized name") {
        auto* app = findApp("hello");
        if (app && app->title == "Hello") PASS();
        else { FAIL(""); if (app) printf("      title='%s'\n", app->title.c_str()); }
    }

    TEST("game app found with icon.png detected") {
        auto* app = findApp("game");
        if (app && !app->iconPath.empty()) {
            bool hasIconPng = app->iconPath.find("icon.png") != P::String::npos;
            if (hasIconPng) PASS();
            else { FAIL(""); printf("      icon='%s'\n", app->iconPath.c_str()); }
        } else { FAIL("no icon"); }
    }

    TEST("apps sorted by name") {
        auto& apps = mgr().apps();
        bool sorted = true;
        for (size_t i = 1; i < apps.size(); i++) {
            if (apps[i].name < apps[i-1].name) { sorted = false; break; }
        }
        if (sorted) PASS(); else FAIL("not sorted");
    }

    TEST("source is HTML for fs apps") {
        auto* app = findApp("calc");
        if (app && app->source == App::AppInfo::HTML) PASS();
        else FAIL("");
    }

    // === systemConfig ===
    printf("\nsystemConfig:\n");

    TEST("config has display.brightness default") {
        int b = mgr().systemConfig.getInt("display.brightness");
        if (b == 255) PASS();
        else { FAIL(""); printf("      brightness=%d\n", b); }
    }

    TEST("config has power.auto_sleep default") {
        bool s = mgr().systemConfig.getBool("power.auto_sleep");
        if (s) PASS(); else FAIL("expected true");
    }

    TEST("config loads from VFS file") {
        VFS::reset();
        setupVFS();
        VFS::writeFile("/system/config.yml",
            "display:\n"
            "  brightness: 100\n"
            "power:\n"
            "  auto_sleep: false\n"
        );
        // Re-init to reload config
        mgr().init();
        int b = mgr().systemConfig.getInt("display.brightness");
        bool s = mgr().systemConfig.getBool("power.auto_sleep");
        if (b == 100 && !s) PASS();
        else { FAIL(""); printf("      brightness=%d auto_sleep=%d\n", b, s); }
    }

    // === launch ===
    printf("\nlaunch:\n");

    // Reset with fresh VFS
    VFS::reset();
    setupVFS();
    mgr().init();

    TEST("launch existing app returns true") {
        bool ok = mgr().launch("calc");
        if (ok) PASS(); else FAIL("");
    }

    TEST("after launch, inLauncher is false") {
        if (!mgr().inLauncher()) PASS(); else FAIL("still in launcher");
    }

    TEST("launch nonexistent app returns false") {
        bool ok = mgr().launch("nonexistent");
        if (!ok) PASS(); else FAIL("should fail");
    }

    // === queueLaunch / processPendingLaunch ===
    printf("\nPending queue:\n");

    VFS::reset();
    setupVFS();
    mgr().init();

    TEST("queueLaunch + processPendingLaunch launches app") {
        mgr().queueLaunch("calc");
        mgr().processPendingLaunch();
        if (!mgr().inLauncher()) PASS(); else FAIL("should be in app");
    }

    TEST("returnToLauncher + process returns to launcher") {
        mgr().launch("hello");
        mgr().returnToLauncher();
        mgr().processPendingLaunch();
        if (mgr().inLauncher()) PASS(); else FAIL("should be in launcher");
    }

    TEST("returnToLauncher cancels pending launch") {
        mgr().queueLaunch("calc");
        mgr().returnToLauncher();
        mgr().processPendingLaunch();
        // returnToLauncher should take priority
        if (mgr().inLauncher()) PASS(); else FAIL("should be in launcher");
    }

    // === refreshApps ===
    printf("\nrefreshApps:\n");

    TEST("refreshApps rescans and picks up new app") {
        // Add a new app to VFS
        VFS::mkdir("/apps/newapp");
        VFS::writeFile("/apps/newapp/newapp.bax",
            "<app title=\"New App\"><ui default=\"/m\"><page id=\"m\"><label>New</label></page></ui></app>"
        );
        mgr().refreshApps();
        auto* app = findApp("newapp");
        if (app && app->title == "New App") PASS();
        else FAIL("new app not found");
    }

    // === Empty /apps ===
    printf("\nEdge cases:\n");

    TEST("no apps dir — init doesn't crash") {
        VFS::reset();
        VFS::mkdir("/system");
        // No /apps at all
        mgr().init();
        // May still have native apps
        PASS(); // if we got here without crash
    }

    TEST("empty apps dir — zero html apps") {
        VFS::reset();
        VFS::mkdir("/apps");
        VFS::mkdir("/system");
        mgr().init();
        int htmlCount = 0;
        for (auto& a : mgr().apps()) {
            if (a.source == App::AppInfo::HTML) htmlCount++;
        }
        if (htmlCount == 0) PASS();
        else { FAIL(""); printf("      html=%d\n", htmlCount); }
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d APP MANAGER TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d APP MANAGER TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
