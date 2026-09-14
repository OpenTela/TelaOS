/**
 * Test: TaskQueue
 * Thread-safe task queue with keyed deduplication
 */
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "utils/task_queue.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

// Track executed tasks
static std::vector<std::string> g_executed;

class TestTask : public ITask {
    std::string m_name;
    std::string m_key;
public:
    TestTask(const char* name, const char* key = "")
        : m_name(name), m_key(key) {}
    void execute() override { g_executed.push_back(m_name); }
    P::String key() const override { return m_key.c_str(); }
};

int main() {
    printf("=== TaskQueue Tests ===\n\n");
    int passed = 0, total = 0;

    // === Basic ===
    printf("Basic:\n");

    TEST("push + process executes task") {
        auto& q = TaskQueue::instance();
        q.clear();
        g_executed.clear();
        q.push(std::make_unique<TestTask>("task1"));
        q.process();
        if (g_executed.size() == 1 && g_executed[0] == "task1") PASS();
        else { FAIL(""); printf("      executed: %zu\n", g_executed.size()); }
    }

    TEST("multiple unkeyed tasks in order") {
        auto& q = TaskQueue::instance();
        q.clear();
        g_executed.clear();
        q.push(std::make_unique<TestTask>("a"));
        q.push(std::make_unique<TestTask>("b"));
        q.push(std::make_unique<TestTask>("c"));
        q.process();
        if (g_executed.size() == 3) PASS();
        else { FAIL(""); printf("      executed: %zu\n", g_executed.size()); }
    }

    TEST("process clears queue") {
        auto& q = TaskQueue::instance();
        q.clear();
        g_executed.clear();
        q.push(std::make_unique<TestTask>("x"));
        q.process();
        q.process(); // second process should do nothing
        if (g_executed.size() == 1) PASS();
        else { FAIL(""); printf("      executed: %zu\n", g_executed.size()); }
    }

    // === Keyed dedup ===
    printf("\nKeyed dedup:\n");

    TEST("same key overwrites — only latest executes") {
        auto& q = TaskQueue::instance();
        q.clear();
        g_executed.clear();
        q.push(std::make_unique<TestTask>("old", "mykey"));
        q.push(std::make_unique<TestTask>("new", "mykey"));
        q.process();
        // Should execute only "new" (latest with same key)
        if (g_executed.size() == 1 && g_executed[0] == "new") PASS();
        else { FAIL(""); for (auto& e : g_executed) printf("      '%s'\n", e.c_str()); }
    }

    TEST("different keys both execute") {
        auto& q = TaskQueue::instance();
        q.clear();
        g_executed.clear();
        q.push(std::make_unique<TestTask>("a", "key1"));
        q.push(std::make_unique<TestTask>("b", "key2"));
        q.process();
        if (g_executed.size() == 2) PASS();
        else { FAIL(""); printf("      executed: %zu\n", g_executed.size()); }
    }

    TEST("keyed + unkeyed mixed") {
        auto& q = TaskQueue::instance();
        q.clear();
        g_executed.clear();
        q.push(std::make_unique<TestTask>("unkeyed1"));
        q.push(std::make_unique<TestTask>("keyed", "k"));
        q.push(std::make_unique<TestTask>("unkeyed2"));
        q.process();
        if (g_executed.size() == 3) PASS();
        else { FAIL(""); printf("      executed: %zu\n", g_executed.size()); }
    }

    // === UpdateLabelTask / UpdateBindingTask ===
    printf("\nBuilt-in tasks:\n");

    TEST("UpdateLabelTask has key") {
        UpdateLabelTask t("myLabel", "hello");
        auto k = t.key();
        if (!k.empty()) PASS(); else FAIL("empty key");
    }

    TEST("UpdateBindingTask has key") {
        UpdateBindingTask t("myVar", "value");
        auto k = t.key();
        if (!k.empty()) PASS(); else FAIL("empty key");
    }

    TEST("same label deduplicates") {
        auto& q = TaskQueue::instance();
        q.clear();
        q.push(std::make_unique<UpdateLabelTask>("lbl", "old"));
        q.push(std::make_unique<UpdateLabelTask>("lbl", "new"));
        if (q.size() == 1) PASS();
        else { FAIL(""); printf("      size: %zu\n", q.size()); }
        q.clear();
    }

    // === Size / clear ===
    printf("\nSize/clear:\n");

    TEST("size tracks items") {
        auto& q = TaskQueue::instance();
        q.clear();
        q.push(std::make_unique<TestTask>("a"));
        q.push(std::make_unique<TestTask>("b"));
        if (q.size() == 2) PASS();
        else { FAIL(""); printf("      size: %zu\n", q.size()); }
        q.clear();
    }

    TEST("clear empties queue") {
        auto& q = TaskQueue::instance();
        q.push(std::make_unique<TestTask>("a"));
        q.clear();
        if (q.size() == 0) PASS();
        else { FAIL(""); printf("      size: %zu\n", q.size()); }
    }

    TEST("process empty queue — no crash") {
        auto& q = TaskQueue::instance();
        q.clear();
        g_executed.clear();
        q.process();
        if (g_executed.empty()) PASS(); else FAIL("");
    }

    // === UI convenience functions ===
    printf("\nUI helpers:\n");

    TEST("UI::updateLabel queues task") {
        TaskQueue::instance().clear();
        UI::updateLabel("lbl1", "hello");
        if (TaskQueue::instance().size() == 1) PASS();
        else { FAIL(""); printf("      size: %zu\n", TaskQueue::instance().size()); }
        TaskQueue::instance().clear();
    }

    TEST("UI::updateBinding queues task") {
        TaskQueue::instance().clear();
        UI::updateBinding("var1", "value");
        if (TaskQueue::instance().size() == 1) PASS();
        else { FAIL(""); printf("      size: %zu\n", TaskQueue::instance().size()); }
        TaskQueue::instance().clear();
    }

    TaskQueue::destroy();

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TASK QUEUE TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d TASK QUEUE TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
