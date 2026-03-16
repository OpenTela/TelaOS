#include "utils/memory_manager.h"
#include "esp_heap_caps.h"

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
            if (size <= DRAM_THRESHOLD) {
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
