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
void* ps_malloc(size_t size);
void* ps_calloc(size_t n, size_t size);
void* ps_realloc(void* ptr, size_t size);
#ifdef __cplusplus
}
#endif
