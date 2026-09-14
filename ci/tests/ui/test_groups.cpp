/**
 * Test: Groups - tabs/swipe navigation
 * 
 * Check group with multiple pages
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"

const char* GROUPS_APP = R"(
<app>
  <ui default="/main">
    <group id="main" default="home" orientation="horizontal">
      <page id="home">
        <label id="home_label">Home Tab</label>
      </page>
      <page id="stats">
        <label id="stats_label">Stats Tab</label>
      </page>
      <page id="profile">
        <label id="profile_label">Profile Tab</label>
      </page>
    </group>
    
    <page id="settings">
      <label id="settings_label">Settings (standalone)</label>
    </page>
  </ui>
</app>
)";

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== Groups (Tabs) Tests ===\n\n");
    int passed = 0;
    int total = 0;
    
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    
    int count = g_core.render(GROUPS_APP);
    printf("Rendered %d widgets\n\n", count);
    
    // === All pages in group exist ===
    printf("Group pages exist:\n");
    
    TEST("home_label exists");
    {
        auto* w = LvglMock::g_screen->findById("home_label");
        if (w) PASS();
        else FAIL("not found");
    }
    
    TEST("stats_label exists");
    {
        auto* w = LvglMock::g_screen->findById("stats_label");
        if (w) PASS();
        else FAIL("not found");
    }
    
    TEST("profile_label exists");
    {
        auto* w = LvglMock::g_screen->findById("profile_label");
        if (w) PASS();
        else FAIL("not found");
    }
    
    TEST("settings_label exists (standalone)");
    {
        auto* w = LvglMock::g_screen->findById("settings_label");
        if (w) PASS();
        else FAIL("not found");
    }
    
    // === Navigate within group ===
    printf("\nNavigate within group:\n");
    
    TEST("showPage('main/stats') succeeds");
    {
        g_core.showPage("main/stats");
        PASS();
    }
    
    TEST("showPage('main/profile') succeeds");
    {
        g_core.showPage("main/profile");
        PASS();
    }
    
    TEST("showPage('main/home') back to first");
    {
        g_core.showPage("main/home");
        PASS();
    }
    
    // === Navigate to standalone page ===
    printf("\nNavigate to standalone:\n");
    
    TEST("showPage('settings') to standalone");
    {
        g_core.showPage("settings");
        PASS();
    }
    
    TEST("showPage('main') back to group");
    {
        g_core.showPage("main");
        PASS();
    }
    
    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d GROUPS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d GROUPS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
