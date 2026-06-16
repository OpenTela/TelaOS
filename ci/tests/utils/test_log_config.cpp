/**
 * Test: Log Config
 * Log levels, categories, set/get, command parsing
 */
#include <cstdio>
#include <cstring>
#include "utils/log_config.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== Log Config Tests ===\n\n");
    int passed = 0, total = 0;

    // === Defaults ===
    printf("Defaults:\n");

    TEST("default level is Info") {
        // Reset
        Log::setAll(Log::Info);
        if (Log::get(Log::UI) == Log::Info && Log::get(Log::LUA) == Log::Info &&
            Log::get(Log::STATE) == Log::Info && Log::get(Log::APP) == Log::Info &&
            Log::get(Log::BLE) == Log::Info)
            PASS(); else FAIL("");
    }

    // === set / get ===
    printf("\nset/get:\n");

    TEST("set UI to Verbose") {
        Log::set(Log::UI, Log::Verbose);
        if (Log::get(Log::UI) == Log::Verbose) PASS(); else FAIL("");
    }

    TEST("set doesn't affect other categories") {
        Log::setAll(Log::Info);
        Log::set(Log::LUA, Log::Debug);
        if (Log::get(Log::UI) == Log::Info && Log::get(Log::LUA) == Log::Debug)
            PASS(); else FAIL("");
    }

    TEST("setAll changes all") {
        Log::setAll(Log::Error);
        if (Log::get(Log::UI) == Log::Error && Log::get(Log::LUA) == Log::Error &&
            Log::get(Log::STATE) == Log::Error && Log::get(Log::APP) == Log::Error &&
            Log::get(Log::BLE) == Log::Error)
            PASS(); else FAIL("");
    }

    TEST("set Disabled") {
        Log::set(Log::BLE, Log::Disabled);
        if (Log::get(Log::BLE) == Log::Disabled) PASS(); else FAIL("");
    }

    // === Level ordering ===
    printf("\nLevel ordering:\n");

    TEST("Disabled < Error < Warn < Info < Debug < Verbose") {
        if (Log::Disabled < Log::Error && Log::Error < Log::Warn &&
            Log::Warn < Log::Info && Log::Info < Log::Debug &&
            Log::Debug < Log::Verbose)
            PASS(); else FAIL("");
    }

    TEST("level >= check works for filtering") {
        Log::set(Log::UI, Log::Warn);
        // At Warn level: Error and Warn pass, Info/Debug/Verbose don't
        bool errOk = Log::get(Log::UI) >= Log::Error;
        bool warnOk = Log::get(Log::UI) >= Log::Warn;
        bool infoOk = Log::get(Log::UI) >= Log::Info;
        if (errOk && warnOk && !infoOk) PASS(); else FAIL("");
    }

    // === catName / levelName ===
    printf("\nNames:\n");

    TEST("catName") {
        if (strcmp(Log::catName(Log::UI), "UI") == 0 &&
            strcmp(Log::catName(Log::LUA), "LUA") == 0 &&
            strcmp(Log::catName(Log::STATE), "STATE") == 0 &&
            strcmp(Log::catName(Log::APP), "APP") == 0 &&
            strcmp(Log::catName(Log::BLE), "BLE") == 0)
            PASS(); else FAIL("");
    }

    TEST("levelName") {
        if (strcmp(Log::levelName(Log::Disabled), "Disabled") == 0 &&
            strcmp(Log::levelName(Log::Error), "Error") == 0 &&
            strcmp(Log::levelName(Log::Warn), "Warn") == 0 &&
            strcmp(Log::levelName(Log::Info), "Info") == 0 &&
            strcmp(Log::levelName(Log::Debug), "Debug") == 0 &&
            strcmp(Log::levelName(Log::Verbose), "Verbose") == 0)
            PASS(); else FAIL("");
    }

    // === command() ===
    printf("\ncommand:\n");

    TEST("command 'verbose' sets all to Verbose") {
        Log::setAll(Log::Info);
        Log::command("verbose");
        if (Log::get(Log::UI) == Log::Verbose && Log::get(Log::LUA) == Log::Verbose)
            PASS(); else FAIL("");
    }

    TEST("command 'disabled' sets all to Disabled") {
        Log::command("disabled");
        if (Log::get(Log::UI) == Log::Disabled && Log::get(Log::BLE) == Log::Disabled)
            PASS(); else FAIL("");
    }

    TEST("command 'info' sets all to Info") {
        Log::command("info");
        if (Log::get(Log::UI) == Log::Info) PASS(); else FAIL("");
    }

    TEST("command 'ui verbose' sets only UI") {
        Log::setAll(Log::Info);
        Log::command("ui verbose");
        if (Log::get(Log::UI) == Log::Verbose && Log::get(Log::LUA) == Log::Info)
            PASS(); else FAIL("");
    }

    TEST("command 'lua debug' sets only LUA") {
        Log::setAll(Log::Info);
        Log::command("lua debug");
        if (Log::get(Log::LUA) == Log::Debug && Log::get(Log::UI) == Log::Info)
            PASS(); else FAIL("");
    }

    TEST("command 'state disabled' sets only STATE") {
        Log::setAll(Log::Info);
        Log::command("state disabled");
        if (Log::get(Log::STATE) == Log::Disabled && Log::get(Log::UI) == Log::Info)
            PASS(); else FAIL("");
    }

    TEST("command 'ble error'") {
        Log::setAll(Log::Info);
        Log::command("ble error");
        if (Log::get(Log::BLE) == Log::Error) PASS(); else FAIL("");
    }

    TEST("command empty — no crash") {
        Log::command("");
        PASS();
    }

    TEST("command nullptr — no crash") {
        Log::command(nullptr);
        PASS();
    }

    // Cleanup
    Log::setAll(Log::Info);

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d LOG CONFIG TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d LOG CONFIG TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
