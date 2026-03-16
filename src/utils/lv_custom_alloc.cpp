/**
 * lv_custom_alloc.cpp — LVGL custom allocator via MemoryManager
 *
 * Delegates all allocation decisions to MemoryManager::instance().
 * Display buffer is NOT affected (uses heap_caps_malloc directly).
 */

#include "utils/memory_manager.h"

extern "C" {

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

void* lv_malloc_core(size_t size) {
    return MemoryManager::instance().alloc(size);
}

void* lv_realloc_core(void* p, size_t new_size) {
    return MemoryManager::instance().realloc(p, new_size);
}

void lv_free_core(void* p) {
    MemoryManager::instance().free(p);
}

} // extern "C"
