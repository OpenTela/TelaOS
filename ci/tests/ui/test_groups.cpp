/**
 * Test: Groups - tabs/swipe navigation
 * 
 * Check group with multiple pages: they belong to the group, navigation reaches each of them.
 * The path of a page in a group is the group's id, "/" and its own id, however long both are.
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

// A path of 85 characters to the default page of the group
const char* LONG_IDS_APP = R"(
<app>
  <ui default="/settings_page_with_a_long_name">
    <page id="settings_page_with_a_long_name">
      <label>Settings</label>
    </page>
    <group id="a_group_whose_name_is_rather_long_for_a_watch" default="and_a_default_page_with_a_long_name_too">
      <page id="first_page">
        <label>First</label>
      </page>
      <page id="and_a_default_page_with_a_long_name_too">
        <label>Default</label>
      </page>
    </group>
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
    
    auto& grp = g_core.app().group(0);
    
    TEST("group 'main' holds its 3 pages");
    {
        if (grp.page_ids.size() == 3 && grp.page_ids[0] == "home" && grp.page_ids[2] == "profile") PASS();
        else FAIL("pages are not in the group");
    }
    
    TEST("only 'settings' is a standalone page");
    {
        if (g_core.app().pagesCount() == 1 && g_core.app().pageId(0) == "settings") PASS();
        else FAIL("group pages leaked into standalone pages");
    }
    
    TEST("default: group shown on 'home'");
    {
        if (g_core.app().currentGroup() == 0 && grp.current_page_idx == 0) PASS();
        else FAIL("group not shown");
    }
    
    TEST("showPage('main/stats') goes to page 1");
    {
        g_core.showPage("main/stats");
        if (g_core.app().currentGroup() == 0 && grp.current_page_idx == 1) PASS();
        else FAIL("not on stats");
    }
    
    TEST("showPage('main/profile') goes to page 2");
    {
        g_core.showPage("main/profile");
        if (grp.current_page_idx == 2) PASS();
        else FAIL("not on profile");
    }
    
    TEST("showPage('main/home') back to first");
    {
        g_core.showPage("main/home");
        if (grp.current_page_idx == 0) PASS();
        else FAIL("not on home");
    }
    
    // === Navigate to standalone page ===
    printf("\nNavigate to standalone:\n");
    
    TEST("showPage('settings') to standalone");
    {
        g_core.showPage("settings");
        if (g_core.app().currentPage() == 0 && g_core.app().currentGroup() < 0) PASS();
        else FAIL("settings not shown");
    }
    
    TEST("showPage('main') back to group, default page");
    {
        g_core.showPage("main");
        if (g_core.app().currentGroup() == 0 && grp.current_page_idx == 0) PASS();
        else FAIL("group not shown");
    }
    
    // === Long ids ===
    printf("\nLong ids (the path is longer than 63 characters):\n");

    g_core.store().clear();
    g_core.render(LONG_IDS_APP);
    const P::String group = "a_group_whose_name_is_rather_long_for_a_watch";
    const P::String path = group + "/and_a_default_page_with_a_long_name_too";
    int gi = -1;  // a render adds to what the app has: the group is found by its id
    for (int i = 0; i < g_core.app().groupsCount(); i++) {
        if (g_core.app().group(i).id == group) gi = i;
    }

    TEST("the page is found by its whole path");
    {
        if (gi < 0) { FAIL("no group"); return 1; }
        auto& g = g_core.app().group(gi);
        if (path.size() > 63 && g.page_objs.size() == 2 && g_core.app().findElement(path.c_str()) == g.page_objs[1]) PASS();
        else FAIL("the page is not under its whole path");
    }

    TEST("showPage(group) goes to its default page");
    {
        g_core.showPage(group.c_str());
        if (g_core.app().currentGroup() == gi && g_core.app().group(gi).current_page_idx == 1) PASS();
        else FAIL("group not shown on its default page");
    }

    TEST("showPage(group/page) goes to the page");
    {
        g_core.showPage((group + "/first_page").c_str());
        if (g_core.app().currentGroup() == gi && g_core.app().group(gi).current_page_idx == 0) PASS();
        else FAIL("not on the first page");
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
