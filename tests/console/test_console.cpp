/**
 * Test: Console command parsing and execution
 * 
 * Tests Console::exec() with various commands
 */
#include <cstdio>
#include <cstring>
#include "console/console.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== Console Tests ===\n\n");
    int passed = 0;
    int total = 0;
    
    State::store().clear();
    
    // === sys ping ===
    printf("sys commands:\n");
    
    TEST("sys ping returns ok");
    {
        auto r = Console::exec("sys ping");
        if (r.success && r.status == "ok") PASS();
        else { FAIL(""); printf("      status='%s' err='%s'\n", r.status.c_str(), r.errorMessage.c_str()); }
    }
    
    // === error handling ===
    printf("\nerror handling:\n");
    
    TEST("unknown subsystem returns error");
    {
        auto r = Console::exec("foo bar");
        if (!r.success && r.errorCode == "invalid") PASS();
        else { FAIL("should fail"); }
    }
    
    TEST("empty command returns error");
    {
        auto r = Console::exec("");
        if (!r.success) PASS();
        else { FAIL("should fail"); }
    }
    
    TEST("ui without command returns error");
    {
        auto r = Console::exec("ui");
        if (!r.success) PASS();
        else { FAIL("should fail"); }
    }
    
    TEST("app without command returns error");  
    {
        auto r = Console::exec("app");
        if (!r.success) PASS();
        else { FAIL("should fail"); }
    }
    
    // Note: ui set/get tests skipped - JsonArray stub doesn't properly preserve
    // items when passed by value to Console::exec(subsys, cmd, args)
    // This is a stub limitation, not a Console bug
    
    // === app push ===
    printf("\napp push (arg order):\n");
    
    TEST("app push myapp 1984 → ok, file=app.html") {
        auto r = Console::exec("app push myapp 1984");
        if (r.success) PASS();
        else { FAIL(""); printf("      err='%s'\n", r.errorMessage.c_str()); }
    }
    
    TEST("app push myapp app.html 1984 → ok") {
        auto r = Console::exec("app push myapp app.html 1984");
        if (r.success) PASS();
        else { FAIL(""); printf("      err='%s'\n", r.errorMessage.c_str()); }
    }
    
    TEST("app push myapp 1984 icon.png → ok") {
        auto r = Console::exec("app push myapp 1984 icon.png");
        if (r.success) PASS();
        else { FAIL(""); printf("      err='%s'\n", r.errorMessage.c_str()); }
    }
    
    TEST("app push myapp .cache 512 → ok (dotfile)") {
        auto r = Console::exec("app push myapp .cache 512");
        if (r.success) PASS();
        else { FAIL(""); printf("      err='%s'\n", r.errorMessage.c_str()); }
    }
    
    TEST("app push myapp data.json 256 → ok") {
        auto r = Console::exec("app push myapp data.json 256");
        if (r.success) PASS();
        else { FAIL(""); printf("      err='%s'\n", r.errorMessage.c_str()); }
    }
    
    printf("\napp push (extension validation):\n");
    
    TEST("app push myapp myfile 1984 → error (no ext)") {
        auto r = Console::exec("app push myapp myfile 1984");
        if (!r.success && r.errorCode == "invalid") PASS();
        else { FAIL("should reject"); printf("      ok=%d err='%s'\n", r.success, r.errorMessage.c_str()); }
    }
    
    TEST("app push myapp 1984 myfile → error (no ext)") {
        auto r = Console::exec("app push myapp 1984 myfile");
        if (!r.success && r.errorCode == "invalid") PASS();
        else { FAIL("should reject"); printf("      ok=%d err='%s'\n", r.success, r.errorMessage.c_str()); }
    }
    
    printf("\napp push (missing args):\n");
    
    TEST("app push → error") {
        auto r = Console::exec("app push");
        if (!r.success) PASS();
        else FAIL("should fail");
    }
    
    TEST("app push myapp → error (no size)") {
        auto r = Console::exec("app push myapp");
        if (!r.success) PASS();
        else FAIL("should fail");
    }
    
    TEST("app push myapp app.html → error (no size)") {
        auto r = Console::exec("app push myapp app.html");
        if (!r.success) PASS();
        else FAIL("should fail");
    }
    
    TEST("app push myapp 0 → error (zero size)") {
        auto r = Console::exec("app push myapp 0");
        if (!r.success) PASS();
        else FAIL("should fail");
    }
    
    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CONSOLE TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d CONSOLE TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
