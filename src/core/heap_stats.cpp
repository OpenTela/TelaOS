#include "core/heap_stats.h"
#include <esp_heap_caps.h>
#include <atomic>
#include <cstdint>
#include <string>

namespace HeapStats {

static std::atomic<size_t> s_failedDram{0}, s_failedPsram{0};
static size_t s_bootDram = 0, s_bootPsram = 0;

// ESP-IDF calls it where the allocation failed, any task: count, nothing more (no allocation,
// no log: the heap may be what is short)
static void onFailed(size_t, uint32_t caps, const char*) {
    if (caps & MALLOC_CAP_SPIRAM) s_failedPsram.fetch_add(1, std::memory_order_relaxed);
    else s_failedDram.fetch_add(1, std::memory_order_relaxed);
}

void begin() {
    s_bootDram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_bootPsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    heap_caps_register_failed_alloc_callback(onFailed);
}

static Heap of(uint32_t caps, const std::atomic<size_t>& failed, size_t atBoot) {
    multi_heap_info_t info{};
    heap_caps_get_info(&info, caps);
    Heap h;
    h.total = heap_caps_get_total_size(caps);
    h.free = info.total_free_bytes;
    h.minFree = info.minimum_free_bytes;
    h.largest = info.largest_free_block;
    h.blocks = info.allocated_blocks;
    h.failed = failed.load(std::memory_order_relaxed);
    h.atBoot = atBoot;
    return h;
}

Heap dram() { return of(MALLOC_CAP_INTERNAL, s_failedDram, s_bootDram); }
Heap psram() { return of(MALLOC_CAP_SPIRAM, s_failedPsram, s_bootPsram); }

static void put(P::String& out, const char* name, const Heap& h) {
    const auto num = [&out](const char* key, size_t v, bool last = false) {
        out += '"';
        out += key;
        out += "\": ";
        out += std::to_string(v).c_str();
        if (!last) out += ", ";
    };
    out += '"';
    out += name;
    out += "\": {";
    num("total", h.total);
    num("free", h.free);
    num("min_free", h.minFree);
    num("largest", h.largest);
    num("blocks", h.blocks);
    num("failed", h.failed);
    num("at_boot", h.atBoot, true);
    out += '}';
}

P::String json(const Heap& dram, const Heap& psram) {
    P::String out = "{";
    put(out, "dram", dram);
    out += ", ";
    put(out, "psram", psram);
    out += '}';
    return out;
}

} // namespace HeapStats
