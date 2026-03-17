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
 *
 * optimizeDRAM() — shrinks display buffer to free DRAM when pressure is high.
 * Called automatically after app render. Restores on return to launcher.
 */

#include <cstddef>
#include <cstdint>

class MemoryManager {
public:
    enum Mode { Auto, RAM, PSRAM };

    static MemoryManager& instance() {
        static MemoryManager inst;
        return inst;
    }

    void setMode(Mode m) { m_mode = m; m_dramLow = false; m_allocCount = 0; }
    Mode mode() const { return m_mode; }
    
    void resetPressure() { m_dramLow = false; m_allocCount = 0; }

    void* alloc(size_t size);
    void* realloc(void* p, size_t size);
    void  free(void* p);

    /// Shrink display buffer until DRAM free >= minFree bytes.
    /// Returns bytes freed (0 if already sufficient).
    size_t optimizeDRAM(size_t minFree = DRAM_MIN_FREE);

private:
    MemoryManager() = default;
    Mode m_mode = Auto;
    bool m_dramLow = false;
    uint16_t m_allocCount = 0;

    static constexpr size_t DRAM_THRESHOLD = 512;
    static constexpr size_t DRAM_PRESSURE  = 50000;
    static constexpr size_t DRAM_MIN_FREE  = 40000;
    static constexpr uint16_t PRESSURE_CHECK_INTERVAL = 32;
};
