// sim_heap.cpp — the watch's DRAM and PSRAM as two TLSF heaps (see sim_heap.h).
//
// malloc/free/calloc/realloc of the whole process are replaced here (glibc lets an
// executable do that): inside a Firmware guard they go to the watch's heaps by the
// watch's rules, otherwise to the host heap. free() and realloc() tell the two apart by
// address, so a block may be freed in any context.

#include "sim_heap.h"

#include "esp_heap_caps.h"  // sim/shim: the caps bits and the functions defined below
#include "tlsf.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
void* __libc_malloc(size_t);
void __libc_free(void*);
void* __libc_calloc(size_t, size_t);
void* __libc_realloc(void*, size_t);
}

namespace {

using SimHeap::DRAM;
using SimHeap::PSRAM;

struct Heap {
    uint8_t* base = nullptr;
    size_t size = 0;
    tlsf_t tlsf = nullptr;
    size_t total = 0, free = 0, min_free = 0, blocks = 0, failed = 0;

    bool owns(const void* p) const {
        return base && static_cast<const uint8_t*>(p) >= base && static_cast<const uint8_t*>(p) < base + size;
    }
};

Heap g_heap[2];
size_t g_size[2] = {SimHeap::kDefaultDram, SimHeap::kDefaultPsram};
bool g_estimated = true;
bool g_ready = false;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
__thread int g_firmware = 0;  // initial-exec TLS: no allocation on first use

struct Lock {
    Lock() {
        while (g_lock.test_and_set(std::memory_order_acquire)) {
        }
    }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};

void ready() {  // under the lock
    if (g_ready) return;
    for (int r = 0; r < 2; ++r) {
        Heap& h = g_heap[r];
        h.size = g_size[r];
        h.base = static_cast<uint8_t*>(__libc_malloc(h.size));
        h.tlsf = h.base ? tlsf_create_with_pool(h.base, h.size, h.size) : nullptr;
        if (!h.tlsf) {
            std::fprintf(stderr, "[heap] cannot create the %s heap (%zu bytes)\n", r == DRAM ? "DRAM" : "PSRAM",
                         h.size);
            std::abort();
        }
        h.total = h.size - tlsf_size(h.tlsf) - tlsf_pool_overhead();
        h.free = h.min_free = h.total;
    }
    g_ready = true;
}

Heap* owner(const void* p) {
    if (!g_ready || !p) return nullptr;  // the heaps never move once made
    for (Heap& h : g_heap) {
        if (h.owns(p)) return &h;
    }
    return nullptr;
}

size_t cost(void* p) { return tlsf_block_size(p) + tlsf_alloc_overhead(); }

void* take(Heap& h, size_t n) {
    void* p = tlsf_malloc(h.tlsf, n);
    if (!p) return nullptr;
    h.free -= cost(p);
    if (h.free < h.min_free) h.min_free = h.free;
    ++h.blocks;
    return p;
}

void give(Heap& h, void* p) {
    h.free += cost(p);
    --h.blocks;
    tlsf_free(h.tlsf, p);
}

// Heaps an allocation may use, best first
int order(size_t n, uint32_t caps, bool plain, int out[2]) {
    if (plain) {  // malloc/new on Arduino-ESP32 with PSRAM
        out[0] = n <= SimHeap::kAlwaysInternal ? DRAM : PSRAM;
        out[1] = out[0] == DRAM ? PSRAM : DRAM;
        return 2;
    }
    if (caps & MALLOC_CAP_SPIRAM) {
        out[0] = PSRAM;
        return 1;
    }
    if (caps & MALLOC_CAP_INTERNAL) {
        out[0] = DRAM;
        return 1;
    }
    out[0] = DRAM;  // 8BIT / DEFAULT: any byte-addressable memory, internal first
    out[1] = PSRAM;
    return 2;
}

void* allocate(size_t n, uint32_t caps, bool plain) {
    if (n == 0) return nullptr;  // heap_caps_malloc(0) is NULL on the chip too
    Lock lock;
    ready();
    int heaps[2];
    const int k = order(n, caps, plain, heaps);
    for (int i = 0; i < k; ++i) {
        if (void* p = take(g_heap[heaps[i]], n)) return p;
    }
    ++g_heap[heaps[0]].failed;
    return nullptr;
}

void release(void* p) {
    if (!p) return;
    if (Heap* h = owner(p)) {
        Lock lock;
        give(*h, p);
        return;
    }
    __libc_free(p);
}

void* reallocate(void* p, size_t n, uint32_t caps, bool plain) {
    if (!p) return allocate(n, caps, plain);
    if (n == 0) {
        release(p);
        return nullptr;
    }
    Heap* h = owner(p);
    if (!h) return __libc_realloc(p, n);  // a host block stays on the host
    int heaps[2];
    const int k = order(n, caps, plain, heaps);
    size_t old_size;
    {
        Lock lock;
        old_size = tlsf_block_size(p);
        for (int i = 0; i < k; ++i) {
            if (&g_heap[heaps[i]] != h) continue;
            const size_t before = cost(p);  // grow or shrink where it is, if allowed and it fits
            if (void* q = tlsf_realloc(h->tlsf, p, n)) {
                h->free = h->free + before - cost(q);
                if (h->free < h->min_free) h->min_free = h->free;
                return q;
            }
        }
    }
    void* q = allocate(n, caps, plain);  // elsewhere: copy, then let the old block go
    if (!q) return nullptr;              // the old block stays valid, as realloc promises
    std::memcpy(q, p, old_size < n ? old_size : n);
    release(p);
    return q;
}

Heap& byCaps(uint32_t caps) { return g_heap[(caps & MALLOC_CAP_SPIRAM) ? PSRAM : DRAM]; }

size_t largest(Heap& h) {
    size_t best = 0;
    tlsf_walk_pool(
        tlsf_get_pool(h.tlsf),
        [](void*, size_t size, int used, void* user) -> bool {
            size_t* b = static_cast<size_t*>(user);
            if (!used && size > *b) *b = size;
            return true;
        },
        &best);
    return best;
}

}  // namespace

namespace SimHeap {

void configure(size_t dram_bytes, size_t psram_bytes) {
    Lock lock;
    if (g_ready) return;
    if (dram_bytes || psram_bytes) g_estimated = false;
    if (dram_bytes) g_size[DRAM] = dram_bytes;
    if (psram_bytes) g_size[PSRAM] = psram_bytes;
}

bool estimated() { return g_estimated; }

Stats stats(Region r) {
    Lock lock;
    ready();
    Heap& h = g_heap[r];
    Stats s;
    s.total = h.total;
    s.free = h.free;
    s.min_free = h.min_free;
    s.largest_free = largest(h);
    s.blocks = h.blocks;
    s.failed = h.failed;
    return s;
}

std::string json() {
    std::string out = "{";
    const char* names[2] = {"dram", "psram"};
    for (int r = 0; r < 2; ++r) {
        const Stats s = stats(static_cast<Region>(r));
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "%s\"%s\": {\"total\": %zu, \"free\": %zu, \"min_free\": %zu, \"largest\": %zu, "
                      "\"blocks\": %zu, \"failed\": %zu}",
                      r ? ", " : "", names[r], s.total, s.free, s.min_free, s.largest_free, s.blocks, s.failed);
        out += buf;
    }
    out += g_estimated ? ", \"estimate\": true}" : ", \"estimate\": false}";
    return out;
}

void* capsMalloc(size_t n, uint32_t caps) { return allocate(n, caps, false); }

void* capsCalloc(size_t count, size_t n, uint32_t caps) {
    size_t total;
    if (__builtin_mul_overflow(count, n, &total)) return nullptr;
    void* p = allocate(total, caps, false);
    if (p) std::memset(p, 0, total);
    return p;
}

void* capsRealloc(void* p, size_t n, uint32_t caps) { return reallocate(p, n, caps, false); }
void capsFree(void* p) { release(p); }

size_t freeSize(uint32_t caps) {
    Lock lock;
    ready();
    return byCaps(caps).free;
}

size_t totalSize(uint32_t caps) {
    Lock lock;
    ready();
    return byCaps(caps).total;
}

size_t minFreeSize(uint32_t caps) {
    Lock lock;
    ready();
    return byCaps(caps).min_free;
}

size_t largestFreeBlock(uint32_t caps) {
    Lock lock;
    ready();
    return largest(byCaps(caps));
}

Firmware::Firmware() { ++g_firmware; }
Firmware::~Firmware() { --g_firmware; }
Host::Host() : saved_(g_firmware) { g_firmware = 0; }
Host::~Host() { g_firmware = saved_; }

}  // namespace SimHeap

// ============ The firmware's API (sim/shim/esp_heap_caps.h) ============

extern "C" {

void* heap_caps_malloc(size_t size, uint32_t caps) { return SimHeap::capsMalloc(size, caps); }
void* heap_caps_calloc(size_t n, size_t size, uint32_t caps) { return SimHeap::capsCalloc(n, size, caps); }
void* heap_caps_realloc(void* ptr, size_t size, uint32_t caps) { return SimHeap::capsRealloc(ptr, size, caps); }
void heap_caps_free(void* ptr) { SimHeap::capsFree(ptr); }
size_t heap_caps_get_free_size(uint32_t caps) { return SimHeap::freeSize(caps); }
size_t heap_caps_get_total_size(uint32_t caps) { return SimHeap::totalSize(caps); }
size_t heap_caps_get_minimum_free_size(uint32_t caps) { return SimHeap::minFreeSize(caps); }
size_t heap_caps_get_largest_free_block(uint32_t caps) { return SimHeap::largestFreeBlock(caps); }
void* ps_malloc(size_t size) { return SimHeap::capsMalloc(size, MALLOC_CAP_SPIRAM); }
void* ps_calloc(size_t n, size_t size) { return SimHeap::capsCalloc(n, size, MALLOC_CAP_SPIRAM); }
void* ps_realloc(void* ptr, size_t size) { return SimHeap::capsRealloc(ptr, size, MALLOC_CAP_SPIRAM); }

// ============ malloc of the whole process ============

void* malloc(size_t n) { return g_firmware > 0 ? allocate(n, 0, true) : __libc_malloc(n); }

void free(void* p) { release(p); }

void* calloc(size_t count, size_t n) {
    if (g_firmware <= 0) return __libc_calloc(count, n);
    size_t total;
    if (__builtin_mul_overflow(count, n, &total)) return nullptr;
    void* p = allocate(total, 0, true);
    if (p) std::memset(p, 0, total);
    return p;
}

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    return reallocate(p, n, 0, true);
}

}  // extern "C"
