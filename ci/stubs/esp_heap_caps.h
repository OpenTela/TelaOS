#pragma once
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
