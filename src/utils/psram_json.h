#pragma once
/**
 * psram_json.h — ArduinoJson allocator that uses PSRAM
 *
 * Usage:
 *   #include "utils/psram_json.h"
 *   auto doc = PsramJsonDoc();
 */

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

struct PsramAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) override {
        void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (!p) p = heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
        return p;
    }
    void deallocate(void* p) override { free(p); }
    void* reallocate(void* p, size_t size) override {
        void* np = heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM);
        if (!np) np = heap_caps_realloc(p, size, MALLOC_CAP_DEFAULT);
        return np;
    }

    static PsramAllocator* instance() {
        static PsramAllocator alloc;
        return &alloc;
    }
};

/// Create a JsonDocument that allocates in PSRAM
inline JsonDocument PsramJsonDoc() {
    return JsonDocument(PsramAllocator::instance());
}
