#pragma once
/**
 * MemoryManager — Controls LVGL memory allocation strategy
 *
 * Modes:
 *   Auto  — small allocs → DRAM, large → PSRAM (default)
 *   RAM   — everything in DRAM (fast, limited)
 *   PSRAM — everything in PSRAM (saves DRAM for display buffer)
 *
 * Used by lv_custom_alloc.cpp for lv_malloc_core / lv_realloc_core.
 * Display buffer bypasses this entirely (uses heap_caps_malloc directly).
 */

#include <cstddef>

class MemoryManager {
public:
    enum Mode { Auto, RAM, PSRAM };

    static MemoryManager& instance() {
        static MemoryManager inst;
        return inst;
    }

    void setMode(Mode m) { m_mode = m; }
    Mode mode() const { return m_mode; }

    void* alloc(size_t size);
    void* realloc(void* p, size_t size);
    void  free(void* p);

private:
    MemoryManager() = default;
    Mode m_mode = Auto;

    static constexpr size_t DRAM_THRESHOLD = 512;
};
