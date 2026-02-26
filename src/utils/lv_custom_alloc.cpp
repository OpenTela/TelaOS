/**
 * lv_custom_alloc.cpp — Split DRAM/PSRAM allocator for LVGL 9.2
 *
 * LV_STDLIB_CUSTOM requires these exact functions:
 *   lv_mem_init()          — called from lv_init()
 *   lv_malloc_core(size)   — called from lv_malloc()
 *   lv_realloc_core(p,sz)  — called from lv_realloc()
 *   lv_free_core(p)        — called from lv_free()
 *
 * Small allocs (draw temps, masks) → DRAM (fast)
 * Large allocs (objects, styles, text) → PSRAM (unlimited)
 */

#include "esp_heap_caps.h"
#include <cstring>

/// Threshold: allocs up to this size try DRAM first
static constexpr size_t DRAM_THRESHOLD = 512;

extern "C" {

void lv_mem_init(void) {
    // No pool to initialize — we use ESP-IDF heap directly
}

void lv_mem_deinit(void) {
    // Nothing to tear down
}

void* lv_malloc_core(size_t size) {
    if (size == 0) return nullptr;

    // Small allocs → try fast DRAM first
    if (size <= DRAM_THRESHOLD) {
        void* p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (p) return p;
        // DRAM full → fallback to PSRAM
    }

    // Large allocs (or DRAM fallback) → PSRAM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
}

void* lv_realloc_core(void* p, size_t new_size) {
    if (!p) return lv_malloc_core(new_size);
    if (new_size == 0) { heap_caps_free(p); return nullptr; }

    // Try in-place first. MALLOC_CAP_8BIT covers both DRAM and PSRAM.
    return heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
}

void lv_free_core(void* p) {
    heap_caps_free(p);  // works for both DRAM and PSRAM
}

} // extern "C"
