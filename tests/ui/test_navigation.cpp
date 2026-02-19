/**
 * Test: Navigation - pages and showPage
 * 
 * Check which widgets are visible on current page
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "core/state_store.h"

const char* NAV_APP = R"(
<app>
  <ui default="/home">
    <page id="home">
      <label id="home_label">Home Page</label>
      <button id="btn_to_settings">Settings</button>
    </page>
    
    <page id="settings">
      <label id="settings_label">Settings Page</label>
      <button id="btn_back">Back</button>
    </page>
    
    <page id="about">
      <label id="about_label">About Page</label>
    </page>
  </ui>
</app>
)";

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

// Find page container by looking for parent of a label
LvglMock::Widget* findPage(const char* labelId) {
    auto* label = LvglMock::g_screen->findById(labelId);
    return label ? label->parent : nullptr;
}

int main() {
    printf("=== Navigation Tests ===\n\n");
    int passed = 0;
    int total = 0;
    
    LvglMock::create_screen(480, 480);
    State::store().clear();
    
    int count = UI::Engine::instance().render(NAV_APP);
    printf("Rendered %d widgets\n\n", count);
    
    // === Initial state (default=/home) ===
    printf("Initial state (default=/home):\n");
    
    TEST("home_label exists");
    {
        auto* w = LvglMock::g_screen->findById("home_label");
        if (w) PASS();
        else FAIL("not found");
    }
    
    TEST("home page not hidden");
    {
        auto* page = findPage("home_label");
        if (page && !page->hidden) PASS();
        else { FAIL(""); if(page) printf("      hidden=%d\n", page->hidden); }
    }
    
    TEST("settings page hidden initially");
    {
        auto* page = findPage("settings_label");
        if (page && page->hidden) PASS();
        else { FAIL("should be hidden"); if(page) printf("      hidden=%d\n", page->hidden); }
    }
    
    TEST("about page hidden initially");
    {
        auto* page = findPage("about_label");
        if (page && page->hidden) PASS();
        else { FAIL("should be hidden"); if(page) printf("      hidden=%d\n", page->hidden); }
    }
    
    // === Navigate to settings ===
    printf("\nNavigate to settings:\n");
    
    UI::Engine::instance().showPage("settings");
    
    TEST("home page hidden after nav to settings");
    {
        auto* page = findPage("home_label");
        if (page && page->hidden) PASS();
        else { FAIL("should be hidden"); if(page) printf("      hidden=%d\n", page->hidden); }
    }
    
    TEST("settings page visible after nav");
    {
        auto* page = findPage("settings_label");
        if (page && !page->hidden) PASS();
        else { FAIL(""); if(page) printf("      hidden=%d\n", page->hidden); }
    }
    
    // === Navigate to about ===
    printf("\nNavigate to about:\n");
    
    UI::Engine::instance().showPage("about");
    
    TEST("settings page hidden after nav to about");
    {
        auto* page = findPage("settings_label");
        if (page && page->hidden) PASS();
        else { FAIL("should be hidden"); }
    }
    
    TEST("about page visible after nav");
    {
        auto* page = findPage("about_label");
        if (page && !page->hidden) PASS();
        else { FAIL(""); }
    }
    
    // === Navigate back to home ===
    printf("\nNavigate back to home:\n");
    
    UI::Engine::instance().showPage("home");
    
    TEST("home page visible after nav back");
    {
        auto* page = findPage("home_label");
        if (page && !page->hidden) PASS();
        else { FAIL(""); }
    }
    
    TEST("about page hidden after nav to home");
    {
        auto* page = findPage("about_label");
        if (page && page->hidden) PASS();
        else { FAIL("should be hidden"); }
    }
    
    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d NAVIGATION TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d NAVIGATION TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
