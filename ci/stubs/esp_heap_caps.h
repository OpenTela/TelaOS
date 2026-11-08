#pragma once
#include <cstdint>
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_DEFAULT 0
inline void* heap_caps_malloc(size_t sz, int) { return malloc(sz); }
inline void* heap_caps_realloc(void* p, size_t sz, int) { return realloc(p, sz); }
inline size_t heap_caps_get_free_size(int) { return 1000000; }
inline size_t heap_caps_get_total_size(int) { return 8000000; }
inline void* ps_malloc(size_t sz) { return malloc(sz); }
inline void heap_caps_free(void* p) { free(p); }
#define MALLOC_CAP_8BIT 4

// What ESP-IDF says of a heap (heap_caps_get_info): TelaCI's numbers, set by tests
typedef struct {
    size_t total_free_bytes;
    size_t total_allocated_bytes;
    size_t largest_free_block;
    size_t minimum_free_bytes;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t total_blocks;
} multi_heap_info_t;
inline multi_heap_info_t g_mock_heap_info = {1000000, 7000000, 900000, 800000, 1234, 12, 1246};
inline void heap_caps_get_info(multi_heap_info_t* info, int) { *info = g_mock_heap_info; }
inline size_t heap_caps_get_minimum_free_size(int) { return g_mock_heap_info.minimum_free_bytes; }
inline size_t heap_caps_get_largest_free_block(int) { return g_mock_heap_info.largest_free_block; }

// An allocation that fails calls it (ESP-IDF 4.4 esp_heap_caps.h); tests call it themselves
typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char* function_name);
inline esp_alloc_failed_hook_t g_mock_alloc_failed = nullptr;
inline int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t f) { g_mock_alloc_failed = f; return 0; }
