/**
 * Test: M:: and P:: allocators as the standard asks of an allocator
 *
 * A container may rebind its allocator to its own node type and build that one from the
 * allocator it was given: "A a(b)" for an allocator b of another type. libstdc++ does not ask
 * it of std::vector<bool>, libc++ does — P::Array<bool> (ui_widget_builder.cpp) did not build
 * under clang with libc++ (the simulator as WebAssembly, Emscripten).
 */
#include <cstdio>
#include <memory>
#include <type_traits>
#include "utils/psram_alloc.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

// What libc++'s std::vector<bool> asks: the storage allocator from the vector's one, and back
static_assert(std::is_nothrow_constructible_v<P::Allocator<unsigned long>, const P::Allocator<bool>&>,
              "P::Allocator<U> must be constructible from P::Allocator<T>");
static_assert(std::is_nothrow_constructible_v<P::Allocator<bool>, const P::Allocator<unsigned long>&>,
              "P::Allocator<T> must be constructible from P::Allocator<U>");
static_assert(std::is_nothrow_constructible_v<M::Allocator<int>, const M::Allocator<char>&>,
              "M::Allocator<U> must be constructible from M::Allocator<T>");
static_assert(std::is_nothrow_default_constructible_v<P::Allocator<int>>, "P::Allocator stays default-constructible");
static_assert(std::is_nothrow_default_constructible_v<M::Allocator<int>>, "M::Allocator stays default-constructible");

int main() {
    printf("=== M:: and P:: allocators ===\n\n");
    int passed = 0, total = 0;

    TEST("P:: rebound allocator allocates and frees") {
        const P::Allocator<bool> forBits;
        P::Allocator<unsigned long> words(forBits);  // what libc++'s vector<bool> does
        unsigned long* p = words.allocate(4);
        p[3] = 42;
        const bool ok = p[3] == 42 && words == forBits;
        words.deallocate(p, 4);
        if (ok)
            PASS();
        else
            FAIL("the rebound allocator");
    }

    TEST("M:: rebound allocator allocates and frees") {
        const M::Allocator<char> forChars;
        M::Allocator<int> ints(forChars);
        int* p = ints.allocate(8);
        p[7] = -7;
        const bool ok = p[7] == -7 && ints == forChars;
        ints.deallocate(p, 8);
        if (ok)
            PASS();
        else
            FAIL("the rebound allocator");
    }

    TEST("allocator_traits rebind builds from the original") {
        using Traits = std::allocator_traits<P::Allocator<bool>>;
        Traits::rebind_alloc<unsigned long> words{P::Allocator<bool>{}};
        unsigned long* p = std::allocator_traits<decltype(words)>::allocate(words, 2);
        p[0] = 1;
        p[1] = 2;
        const bool ok = p[0] + p[1] == 3;
        std::allocator_traits<decltype(words)>::deallocate(words, p, 2);
        if (ok)
            PASS();
        else
            FAIL("rebind_alloc");
    }

    TEST("P::Array<bool>: push, copy, compare") {
        P::Array<bool> bits;
        for (int i = 0; i < 100; ++i)
            bits.push_back(i % 3 == 0);
        const P::Array<bool> copy = bits;
        const bool ok = copy.size() == 100 && copy[99] && !copy[98] && copy == bits;
        if (ok)
            PASS();
        else
            FAIL("P::Array<bool>");
    }

    printf("\n%d/%d passed\n", passed, total);
    return passed == total ? 0 : 1;
}
