#pragma once

#include <cstddef>
#include "utils/psram_alloc.h"

/**
 * HeapStats - the watch's two heaps as the simulator reports them (sim/sim_heap.h, "mem" of a
 * screen's tree): the same numbers, so the simulator's estimate can be checked against a watch
 * (sim/tools/mem_compare.py).
 *
 * DRAM is MALLOC_CAP_INTERNAL, as ESP.getFreeHeap() counts it; PSRAM is MALLOC_CAP_SPIRAM.
 * failed: allocations that got nothing since begin(), each counted at the heap tried first:
 * PSRAM if it asked for PSRAM, DRAM otherwise. atBoot: what was free when the firmware began
 * (begin(), first thing in setup()): the size the simulator gives the firmware (--dram-kb,
 * --psram-kb).
 *
 * Console: sys mem.
 */
namespace HeapStats {

struct Heap {
    size_t total = 0;     // bytes the heap manages
    size_t free = 0;      // free now
    size_t minFree = 0;   // the lowest since boot
    size_t largest = 0;   // the largest block that would succeed now
    size_t blocks = 0;    // live allocations
    size_t failed = 0;    // allocations that got nothing since begin()
    size_t atBoot = 0;    // free when begin() ran
};

/// First thing in setup(): what is free now, and failed allocations from now on
void begin();

Heap dram();
Heap psram();

/// Both, as JSON: {"dram": {"total": ..., "free": ..., "min_free": ..., "largest": ...,
/// "blocks": ..., "failed": ..., "at_boot": ...}, "psram": {...}} - the keys of the simulator's "mem"
P::String json(const Heap& dram, const Heap& psram);

} // namespace HeapStats
