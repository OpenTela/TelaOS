// sim_heap.h — the watch's memory, honestly: internal DRAM and PSRAM as two TLSF heaps.
//
// On the watch every allocation of the firmware lands in one of two heaps:
//   heap_caps_malloc(n, caps)  where the caps say (INTERNAL → DRAM, SPIRAM → PSRAM,
//                              8BIT/DEFAULT → DRAM first, then PSRAM)
//   malloc / new               ≤ 4096 bytes → DRAM first; larger → PSRAM first
//                              (Arduino-ESP32: CONFIG_SPIRAM_USE_MALLOC,
//                               CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096)
// The simulator does the same with the allocator ESP-IDF builds its heap on (TLSF), so the
// free size, the low-water mark and the largest free block behave like the chip's — and an
// allocation that does not fit fails, as it would there.
//
// Plain malloc/new are the watch's only while the firmware runs (Firmware guard around
// setup()/loop()); the simulator's own window, robot and PNG code use the host heap.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace SimHeap {

enum Region { DRAM = 0, PSRAM = 1 };

// Default sizes: what the watch has for the firmware. From the log of a real watch (esp4848s040,
// BLE on, Arduino's 8 KB loop task, 2026-11-09), the clicker drawn: "[Heap] After render:
// DRAM=82236 PSRAM=7475699" there, 51 648 and 481 661 bytes less than the first estimate (320 KB,
// 8 MB) gave the same firmware at the same moment here. On the watch and not here they take BLE,
// ESP-IDF and Arduino (DRAM) and the panel's frame, 480 x 480 x 2 (PSRAM)
constexpr size_t kDefaultDram = 320 * 1024 - 51648;
constexpr size_t kDefaultPsram = 8 * 1024 * 1024 - 481661;
// The loop task of that watch. A larger one (SET_LOOP_TASK_STACK_SIZE) is DRAM the watch takes
// before setup(): the firmware gets that much less (sim_heap.cpp)
constexpr size_t kCalibratedLoopStack = 8192;
constexpr size_t kAlwaysInternal = 4096;  // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL

struct Stats {
    size_t total = 0;         // bytes the heap manages
    size_t free = 0;          // free now (what heap_caps_get_free_size reports)
    size_t min_free = 0;      // low-water mark since start
    size_t largest_free = 0;  // largest single block that would succeed now
    size_t blocks = 0;        // live allocations
    size_t failed = 0;        // allocations that did not fit
};

// Before the first allocation; later calls are ignored. 0 = default. The simulator's --dram-kb
// and --psram-kb are read earlier, before the firmware's static constructors (sim_heap.cpp)
void configure(size_t dram_bytes, size_t psram_bytes);

Stats stats(Region r);
std::string json();  // {"dram": {...}, "psram": {...}}

// The firmware's explicit allocations (esp_heap_caps.h)
void* capsMalloc(size_t n, uint32_t caps);
void* capsCalloc(size_t count, size_t n, uint32_t caps);
void* capsRealloc(void* p, size_t n, uint32_t caps);
void capsFree(void* p);
size_t freeSize(uint32_t caps);
size_t totalSize(uint32_t caps);
size_t minFreeSize(uint32_t caps);
size_t largestFreeBlock(uint32_t caps);

// Plain malloc/new count as the watch's while one of these is alive (nesting allowed)
struct Firmware {
    Firmware();
    ~Firmware();
};
// The simulator's own work inside firmware time (window, robot): the host heap again
struct Host {
    Host();
    ~Host();

private:
    int saved_;
};

}  // namespace SimHeap
