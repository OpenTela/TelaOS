#!/usr/bin/env python3
"""Tests for the simulator's model of the watch's memory (sim/sim_heap.cpp, the TLSF in
sim/third_party/tlsf): each test compiles a small C/C++ program with them and runs it.

    python3 -m unittest discover -s sim/tools -p 'test_*.py'
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

SIM = Path(__file__).resolve().parent.parent
TLSF = SIM / "third_party" / "tlsf"
CC, CXX = shutil.which("cc") or shutil.which("gcc"), shutil.which("c++") or shutil.which("g++")

TLSF_FUZZ = r"""
#include <stdio.h>
#include <stdlib.h>
#include "tlsf.h"
/* Random malloc/realloc/free on a 300 KB pool; the heap must stay consistent all along */
int main(void) {
    size_t size = 300 * 1024;
    void* mem = malloc(size);
    tlsf_t t = tlsf_create_with_pool(mem, size, size);
    if (!t || tlsf_check(t) != 0) { printf("fresh heap broken\n"); return 1; }
    void* live[512] = {0};
    unsigned seed = 12345;
    for (int i = 0; i < 200000; ++i) {
        seed = seed * 1103515245u + 12345u;
        int k = (seed >> 8) % 512;
        size_t n = 1 + (seed >> 12) % ((seed & 3) ? 256 : 20000);
        if (live[k]) {
            if (seed & 0x100) { tlsf_free(t, live[k]); live[k] = 0; }
            else { void* q = tlsf_realloc(t, live[k], n); if (q) live[k] = q; }
        } else {
            live[k] = tlsf_malloc(t, n);
        }
        if (i % 1000 == 0 && tlsf_check(t) != 0) { printf("broken at step %d\n", i); return 1; }
    }
    for (int k = 0; k < 512; ++k) if (live[k]) tlsf_free(t, live[k]);
    printf("ok %d\n", tlsf_check(t));
    return tlsf_check(t) != 0;
}
"""

HEAP_MODEL = r"""
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "sim_heap.h"
#include "esp_heap_caps.h"
using namespace SimHeap;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)
int main() {
    configure(64 * 1024, 1024 * 1024);
    const Stats d0 = stats(DRAM), p0 = stats(PSRAM);
    CHECK(!estimated());
    CHECK(d0.total > 60 * 1024 && d0.total <= 64 * 1024);

    // Host code (outside Firmware) does not touch the watch's heaps
    { std::vector<char> host(100000); CHECK(stats(DRAM).free == d0.free && stats(PSRAM).free == p0.free); }

    // Firmware: small malloc → DRAM, large → PSRAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
    void *small, *large;
    {
        Firmware fw;
        small = std::malloc(100);
        large = std::malloc(10000);
    }
    CHECK(stats(DRAM).free < d0.free && stats(DRAM).blocks == d0.blocks + 1);
    CHECK(stats(PSRAM).free < p0.free && stats(PSRAM).blocks == p0.blocks + 1);
    std::free(small);  // freed outside firmware time: still goes home
    std::free(large);
    CHECK(stats(DRAM).free == d0.free && stats(PSRAM).free == p0.free);

    // new/std::string in firmware time are the watch's too
    {
        Firmware fw;
        std::string s(200, 'x');
        CHECK(stats(DRAM).blocks == d0.blocks + 1);
    }
    CHECK(stats(DRAM).blocks == d0.blocks);

    // heap_caps: caps choose the heap; DRAM only means DRAM only
    void* in = heap_caps_malloc(5000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void* ps = heap_caps_malloc(100, MALLOC_CAP_SPIRAM);
    CHECK(in && stats(DRAM).blocks == d0.blocks + 1);
    CHECK(ps && stats(PSRAM).blocks == p0.blocks + 1);
    CHECK(heap_caps_malloc(100 * 1024, MALLOC_CAP_INTERNAL) == nullptr);  // does not fit: NULL, as on the chip
    CHECK(stats(DRAM).failed == 1);
    void* any = heap_caps_malloc(100 * 1024, MALLOC_CAP_8BIT);  // any byte memory: PSRAM takes it
    CHECK(any && stats(PSRAM).blocks == p0.blocks + 2);
    heap_caps_free(in); heap_caps_free(ps); heap_caps_free(any);

    // The low-water mark stays; the largest block shows fragmentation
    {
        Firmware fw;
        std::vector<void*> blocks;
        for (int i = 0; i < 40; ++i) blocks.push_back(std::malloc(1000));
        for (size_t i = 0; i < blocks.size(); i += 2) std::free(blocks[i]);
        const Stats s = stats(DRAM);
        CHECK(s.largest_free < s.free);  // holes: the free bytes are not one block
        for (size_t i = 1; i < blocks.size(); i += 2) std::free(blocks[i]);
    }
    CHECK(stats(DRAM).free == d0.free);
    CHECK(stats(DRAM).min_free < d0.free - 30000);
    CHECK(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) == stats(DRAM).free);
    CHECK(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) == stats(DRAM).min_free);

    // realloc keeps the contents, wherever the block ends up
    {
        Firmware fw;
        char* p = static_cast<char*>(std::malloc(16));
        std::strcpy(p, "watch");
        p = static_cast<char*>(std::realloc(p, 50000));  // grows past 4 KB: may move to PSRAM
        CHECK(p && std::strcmp(p, "watch") == 0);
        std::free(p);
    }
    CHECK(stats(DRAM).free == d0.free && stats(PSRAM).free == p0.free);

    // Host inside firmware time: the window, the robot
    {
        Firmware fw;
        Host host;
        std::string s(300, 'y');
        CHECK(stats(DRAM).blocks == d0.blocks);
    }
    std::printf(failures ? "%d failed\n" : "ok\n", failures);
    return failures != 0;
}
"""


def run(cmd, cwd):
    p = subprocess.run(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    return p.returncode, p.stdout.decode("utf-8", "replace")


@unittest.skipUnless(CC and CXX, "needs a C and a C++ compiler")
class SimHeap(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_tlsf_stays_consistent_on_this_host(self):
        """the 64-bit fix in control_construct: tlsf_check on a fresh heap and under load"""
        (self.dir / "fuzz.c").write_text(TLSF_FUZZ)
        rc, out = run([CC, "-std=c11", "-O1", "-I", str(TLSF), "fuzz.c", str(TLSF / "tlsf.c"), "-o", "fuzz"],
                      self.dir)
        self.assertEqual(rc, 0, out)
        rc, out = run(["./fuzz"], self.dir)
        self.assertEqual((rc, out.strip()), (0, "ok 0"))

    def test_heap_model(self):
        (self.dir / "model.cpp").write_text(HEAP_MODEL)
        (self.dir / "tlsf.o").unlink(missing_ok=True)
        rc, out = run([CC, "-std=c11", "-O1", "-c", "-I", str(TLSF), str(TLSF / "tlsf.c"), "-o", "tlsf.o"],
                      self.dir)
        self.assertEqual(rc, 0, out)
        # -fno-builtin: the compiler may drop a malloc/free pair it can see through
        rc, out = run([CXX, "-std=c++17", "-O1", "-fno-builtin", "-I", str(SIM), "-I", str(SIM / "shim"),
                       "-I", str(TLSF), "model.cpp", str(SIM / "sim_heap.cpp"), "tlsf.o", "-o", "model"], self.dir)
        self.assertEqual(rc, 0, out)
        rc, out = run(["./model"], self.dir)
        self.assertEqual((rc, out.strip()), (0, "ok"), out)


if __name__ == "__main__":
    unittest.main()
