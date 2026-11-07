// esp_heap_caps.h for the simulator: the watch's heaps, not the host's (sim_heap.cpp).
// TelaCI keeps its own stub (ci/stubs/esp_heap_caps.h); the bits are the same.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_DEFAULT 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4

#ifdef __cplusplus
extern "C" {
#endif
void* heap_caps_malloc(size_t size, uint32_t caps);
void* heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps);
void heap_caps_free(void* ptr);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
typedef struct {
    size_t total_free_bytes;
    size_t total_allocated_bytes;
    size_t largest_free_block;
    size_t minimum_free_bytes;
    size_t allocated_blocks;
    size_t free_blocks;
    size_t total_blocks;
} multi_heap_info_t;
void heap_caps_get_info(multi_heap_info_t* info, uint32_t caps);
// Called when an allocation fails, as ESP-IDF calls it: what the watch counts, the simulator does too
typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char* function_name);
int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t callback);
void* ps_malloc(size_t size);
void* ps_calloc(size_t n, size_t size);
void* ps_realloc(void* ptr, size_t size);
#ifdef __cplusplus
}
#endif
