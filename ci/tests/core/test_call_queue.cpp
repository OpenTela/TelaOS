/**
 * Test: CallQueue
 * Deferred function call queue — push, process, dedup, overflow
 */
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "core/call_queue.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static std::vector<std::string> g_calls;

static void handler(const P::String& name) {
    g_calls.push_back(std::string(name.c_str()));
}

int main() {
    printf("=== CallQueue Tests ===\n\n");
    int passed = 0, total = 0;

    // === Basic ===
    printf("Basic:\n");

    TEST("init + shutdown no crash") {
        CallQueue::init();
        CallQueue::shutdown();
        PASS();
    }

    TEST("push + process calls handler") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::push("myFunc");
        CallQueue::process();
        CallQueue::shutdown();
        if (g_calls.size() == 1 && g_calls[0] == "myFunc") PASS();
        else { FAIL(""); printf("      calls: %zu\n", g_calls.size()); }
    }

    TEST("multiple pushes in order") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::push("first");
        CallQueue::push("second");
        CallQueue::push("third");
        CallQueue::process();
        CallQueue::shutdown();
        if (g_calls.size() == 3 && g_calls[0] == "first" && g_calls[1] == "second" && g_calls[2] == "third")
            PASS();
        else { FAIL(""); printf("      calls: %zu\n", g_calls.size()); }
    }

    // === Dedup ===
    printf("\nDedup:\n");

    TEST("duplicate push deduplicated") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::push("tick");
        CallQueue::push("tick");
        CallQueue::push("tick");
        CallQueue::process();
        CallQueue::shutdown();
        if (g_calls.size() == 1 && g_calls[0] == "tick") PASS();
        else { FAIL(""); printf("      calls: %zu\n", g_calls.size()); }
    }

    TEST("different names not deduplicated") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::push("a");
        CallQueue::push("b");
        CallQueue::push("a");  // dup of first — skipped
        CallQueue::process();
        CallQueue::shutdown();
        if (g_calls.size() == 2 && g_calls[0] == "a" && g_calls[1] == "b") PASS();
        else { FAIL(""); for (auto& c : g_calls) printf("      '%s'\n", c.c_str()); }
    }

    TEST("after process, same name can be pushed again") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::push("tick");
        CallQueue::process();
        CallQueue::push("tick");
        CallQueue::process();
        CallQueue::shutdown();
        if (g_calls.size() == 2) PASS();
        else { FAIL(""); printf("      calls: %zu\n", g_calls.size()); }
    }

    // === Size / hasPending ===
    printf("\nSize:\n");

    TEST("size tracks pending") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        CallQueue::push("a");
        CallQueue::push("b");
        if (CallQueue::size() == 2) PASS();
        else { FAIL(""); printf("      size: %zu\n", CallQueue::size()); }
        CallQueue::shutdown();
    }

    TEST("hasPending true when items queued") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        CallQueue::push("x");
        if (CallQueue::hasPending()) PASS(); else FAIL("false");
        CallQueue::shutdown();
    }

    TEST("hasPending false after process") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::push("x");
        CallQueue::process();
        if (!CallQueue::hasPending()) PASS(); else FAIL("true");
        CallQueue::shutdown();
    }

    // === Edge cases ===
    printf("\nEdge cases:\n");

    TEST("empty push ignored") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::push("");
        CallQueue::process();
        CallQueue::shutdown();
        if (g_calls.empty()) PASS(); else { FAIL(""); printf("      calls: %zu\n", g_calls.size()); }
    }

    TEST("process without handler — no crash") {
        CallQueue::init();
        CallQueue::setHandler(nullptr);
        CallQueue::push("test");
        CallQueue::process();
        CallQueue::shutdown();
        PASS();
    }

    TEST("process empty queue — no crash") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        CallQueue::process();
        CallQueue::shutdown();
        if (g_calls.empty()) PASS(); else FAIL("");
    }

    TEST("overflow drops calls") {
        CallQueue::init();
        CallQueue::setHandler(handler);
        g_calls.clear();
        // Push 20 unique items (max is 16)
        for (int i = 0; i < 20; i++) {
            char name[16];
            snprintf(name, sizeof(name), "func_%d", i);
            CallQueue::push(name);
        }
        uint32_t dropped = CallQueue::droppedCount();
        CallQueue::process();
        CallQueue::shutdown();
        if (dropped > 0 && g_calls.size() == 16) PASS();
        else { FAIL(""); printf("      dropped=%u calls=%zu\n", dropped, g_calls.size()); }
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CALL QUEUE TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d CALL QUEUE TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
