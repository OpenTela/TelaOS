#pragma once
/**
 * lv_custom_alloc.h — Split DRAM/PSRAM allocator for LVGL 9.2
 *
 * LVGL LV_STDLIB_CUSTOM mode expects these symbols at link time.
 * No need to include this header in lv_conf.h — LVGL finds them by name.
 */

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

void  lv_mem_init(void);
void  lv_mem_deinit(void);
void* lv_malloc_core(size_t size);
void* lv_realloc_core(void* p, size_t new_size);
void  lv_free_core(void* p);

#ifdef __cplusplus
}
#endif
