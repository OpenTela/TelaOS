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

// Default sizes: an ESTIMATE of what the watch has free for the firmware after boot,
// until calibrated against `sys info` on a real watch
constexpr size_t kDefaultDram = 320 * 1024;
constexpr size_t kDefaultPsram = 8 * 1024 * 1024;
constexpr size_t kAlwaysInternal = 4096;  // CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL

struct Stats {
    size_t total = 0;         // bytes the heap manages
    size_t free = 0;          // free now (what heap_caps_get_free_size reports)
    size_t min_free = 0;      // low-water mark since start
    size_t largest_free = 0;  // largest single block that would succeed now
    size_t blocks = 0;        // live allocations
    size_t failed = 0;        // allocations that did not fit
};

// Before the first allocation; later calls are ignored. 0 = default
void configure(size_t dram_bytes, size_t psram_bytes);
bool estimated();  // sizes are the defaults, not calibrated

Stats stats(Region r);
std::string json();  // {"dram": {...}, "psram": {...}, "estimate": true}

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
