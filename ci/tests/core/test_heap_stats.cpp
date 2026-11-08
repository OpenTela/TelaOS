/**
 * Test: HeapStats — the watch's heaps as the simulator reports them, for sys mem.
 *
 * The TelaCI stub of esp_heap_caps.h gives heap_caps_get_info() the numbers of g_mock_heap_info
 * and keeps the failed-allocation hook HeapStats registers: the test calls it as ESP-IDF would.
 */
#include <cstdio>
#include <cstdlib>
#include <string>
#include "core/heap_stats.h"
#include "console/console.h"
#include "esp_heap_caps.h"
#include "utils/log_config.h"

#define TEST(name) printf("  %-56s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

int main() {
    printf("=== HeapStats Tests ===\n\n");
    int passed = 0, total = 0;
    Log::setAll(Log::Disabled);

    TEST("the numbers of heap_caps_get_info");
    {
        g_mock_heap_info = multi_heap_info_t{};
        g_mock_heap_info.total_free_bytes = 114452;
        g_mock_heap_info.minimum_free_bytes = 107492;
        g_mock_heap_info.largest_free_block = 113976;
        g_mock_heap_info.allocated_blocks = 575;
        const HeapStats::Heap d = HeapStats::dram();
        const bool fromInfo = d.free == 114452 && d.minFree == 107492 && d.largest == 113976 && d.blocks == 575;
        const bool totalOfHeap = d.total == heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
        if (fromInfo && totalOfHeap)
            PASS();
        else
            FAIL("");
    }

    TEST("nothing failed before begin()");
    {
        const bool noneFailed = HeapStats::dram().failed == 0 && HeapStats::psram().failed == 0;
        if (noneFailed && !g_mock_alloc_failed)
            PASS();
        else
            FAIL("");
    }

    TEST("begin(): a failed allocation is counted at its heap");
    {
        HeapStats::begin();
        if (g_mock_alloc_failed) {
            g_mock_alloc_failed(5000, MALLOC_CAP_SPIRAM, "heap_caps_malloc");
            g_mock_alloc_failed(64, MALLOC_CAP_DEFAULT, "malloc");
            g_mock_alloc_failed(64, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, "heap_caps_malloc");
        }
        const bool counted = HeapStats::psram().failed == 1 && HeapStats::dram().failed == 2;
        if (counted)
            PASS();
        else
            FAIL("");
    }

    TEST("json: the keys of the simulator's mem");
    {
        HeapStats::Heap d, p;
        d.total = 327680, d.free = 114452, d.minFree = 107492, d.largest = 113976, d.blocks = 575, d.atBoot = 290000;
        p.total = 8383840, p.free = 7655096, p.failed = 2;
        const P::String j = HeapStats::json(d, p);
        const char* want = "{\"dram\": {\"total\": 327680, \"free\": 114452, \"min_free\": 107492, \"largest\": 113976, "
                           "\"blocks\": 575, \"failed\": 0, \"at_boot\": 290000}, \"psram\": {\"total\": 8383840, "
                           "\"free\": 7655096, \"min_free\": 0, \"largest\": 0, \"blocks\": 0, \"failed\": 2, \"at_boot\": 0}}";
        if (j == want)
            PASS();
        else {
            FAIL("");
            printf("      got %s\n", j.c_str());
        }
    }

    TEST("sys mem: the JSON as text");
    {
        auto r = Console::exec("sys mem");
        const std::string text = r.payload ? std::string((const char*)r.payload, r.payloadSize) : "";
        free(r.payload);
        const bool isText = r.success && r.type == Console::ResponseType::StringData;
        const bool both = text.rfind("{\"dram\": {\"total\": ", 0) == 0 && text.find("\"psram\": {") != std::string::npos;
        if (isText && both)
            PASS();
        else
            FAIL("");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d HEAPSTATS TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d HEAPSTATS TESTS PASSED ===\n", passed, total);
    return 1;
}
