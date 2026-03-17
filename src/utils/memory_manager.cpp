#include "utils/memory_manager.h"
#include "esp_heap_caps.h"
#include "hal/display_hal.h"
#include <Arduino.h>

void* MemoryManager::alloc(size_t size) {
    if (size == 0) return nullptr;

    switch (m_mode) {
        case RAM: {
            void* p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (p) return p;
            // DRAM exhausted → fallback to PSRAM
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        }

        case PSRAM:
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

        case Auto:
        default: {
            // Adaptive: when DRAM is low, send everything to PSRAM
            size_t dramFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            if (size <= DRAM_THRESHOLD && dramFree > DRAM_PRESSURE) {
                void* p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (p) return p;
            }
            return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        }
    }
}

void* MemoryManager::realloc(void* p, size_t new_size) {
    if (!p) return alloc(new_size);
    if (new_size == 0) { free(p); return nullptr; }

    // realloc in-place where possible (CAP_8BIT covers both DRAM and PSRAM)
    return heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
}

void MemoryManager::free(void* p) {
    heap_caps_free(p);
}

size_t MemoryManager::optimizeDRAM(size_t minFree) {
    size_t dramBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    
    if (dramBefore >= minFree) return 0;  // already OK
    
    // Shrink display buffer stepwise: Max → Optimal → Small → Micro
    static const DisplayBuffer steps[] = { BufferOptimal, BufferSmall, BufferMicro };
    int currentLines = display_get_buffer_lines();
    
    for (int i = 0; i < 3; i++) {
        int targetLines = static_cast<int>(steps[i]);
        if (targetLines >= currentLines) continue;  // skip larger or equal
        
        Serial.printf("[MemMgr] DRAM pressure: %u < %u, shrink buffer %d -> %d lines\n",
                      (unsigned)dramBefore, (unsigned)minFree,
                      currentLines, targetLines);
        
        if (display_set_buffer(steps[i])) {
            currentLines = targetLines;
            size_t dramNow = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            Serial.printf("[MemMgr] DRAM after shrink: %u bytes free\n", (unsigned)dramNow);
            
            if (dramNow >= minFree) {
                return dramNow - dramBefore;
            }
        }
    }
    
    size_t dramAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    return (dramAfter > dramBefore) ? dramAfter - dramBefore : 0;
}
