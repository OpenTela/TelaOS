/**
 * Test: Console `sys ota` command parsing & arming.
 *
 * Mirrors test_console.cpp style. Exercises argument handling through
 * Console::exec(); a successful parse arms OtaReceive (allocates the PSRAM
 * buffer under the host stub), which we disarm with OtaReceive::cancel().
 */
#include <cstdio>
#include <cstring>
#include "console/console.h"
#include "core/core.h"
#include "ota/ota_receive.h"

#define TEST(name) printf("  %-52s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

int main() {
    printf("=== Console sys ota Tests ===\n\n");
    int passed = 0, total = 0;

    g_core.store().clear();

    printf("valid forms:\n");

    TEST("sys ota <raw> <comp> <sha> -> ok") {
        auto r = Console::exec("sys ota 2000 1500 "
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        bool ok = r.success;
        OtaReceive::cancel();
        if (ok) PASS();
        else { FAIL(""); printf("      err='%s'\n", r.errorMessage.c_str()); }
    }

    TEST("sys ota <raw> 0 (uncompressed, no hash) -> ok") {
        auto r = Console::exec("sys ota 2000 0");
        bool ok = r.success;
        OtaReceive::cancel();
        if (ok) PASS();
        else { FAIL(""); printf("      err='%s'\n", r.errorMessage.c_str()); }
    }

    printf("\nrejected forms:\n");

    TEST("sys ota (no args) -> invalid") {
        auto r = Console::exec("sys ota");
        if (!r.success && r.errorCode == "invalid") PASS();
        else FAIL("should reject");
    }

    TEST("sys ota 0 0 (zero raw) -> error") {
        auto r = Console::exec("sys ota 0 0");
        OtaReceive::cancel();
        if (!r.success) PASS();
        else FAIL("should reject");
    }

    TEST("sys ota <oversize> 0 -> error (> 3MB)") {
        auto r = Console::exec("sys ota 3145729 0");  // 0x300000 + 1
        OtaReceive::cancel();
        if (!r.success) PASS();
        else FAIL("should reject");
    }

    TEST("sys ota 1000 5000000 -> error (comp > bound)") {
        auto r = Console::exec("sys ota 1000 5000000");
        OtaReceive::cancel();
        if (!r.success) PASS();
        else FAIL("should reject");
    }

    TEST("sys ota 2000 -1 -> invalid (negative comp)") {
        auto r = Console::exec("sys ota 2000 -1");
        OtaReceive::cancel();
        if (!r.success && r.errorCode == "invalid") PASS();
        else FAIL("should reject");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CONSOLE OTA TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d CONSOLE OTA TESTS PASSED ===\n", passed, total);
    return 1;
}
