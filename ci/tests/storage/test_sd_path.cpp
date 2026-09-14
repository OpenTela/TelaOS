/**
 * Test: SD path resolver
 * Pure path-normalization + directory-traversal rejection (no hardware).
 */
#include <cstdio>
#include <cstring>
#include "hal/sd_path.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("\u2713\n"); passed++; } while(0)
#define FAIL(msg) printf("\u2717 %s\n", msg)

static bool resolves(const char* mount, const char* in, const char* expect) {
    char out[128];
    if (!sdResolvePath(mount, in, out, sizeof(out))) return false;
    return strcmp(out, expect) == 0;
}
static bool rejects(const char* mount, const char* in) {
    char out[128];
    return !sdResolvePath(mount, in, out, sizeof(out));
}

int main() {
    printf("=== SD Path Resolver Tests ===\n\n");
    int passed = 0, total = 0;

    printf("Resolve:\n");
    TEST("relative -> mount/relative") {
        if (resolves("/sd", "note.txt", "/sd/note.txt")) PASS(); else FAIL("");
    }
    TEST("leading slash") {
        if (resolves("/sd", "/note.txt", "/sd/note.txt")) PASS(); else FAIL("");
    }
    TEST("already mount-prefixed (no doubling)") {
        if (resolves("/sd", "/sd/note.txt", "/sd/note.txt")) PASS(); else FAIL("");
    }
    TEST("nested path") {
        if (resolves("/sd", "a/b/c.txt", "/sd/a/b/c.txt")) PASS(); else FAIL("");
    }
    TEST("mount itself") {
        if (resolves("/sd", "/sd", "/sd")) PASS(); else FAIL("");
    }
    TEST("trailing slash on mount normalized") {
        if (resolves("/sd/", "x.txt", "/sd/x.txt")) PASS(); else FAIL("");
    }
    TEST("dots inside a name are fine (a..b)") {
        if (resolves("/sd", "a..b.txt", "/sd/a..b.txt")) PASS(); else FAIL("");
    }

    printf("\nReject traversal / overflow:\n");
    TEST("rejects ..") {
        if (rejects("/sd", "..")) PASS(); else FAIL("");
    }
    TEST("rejects ../etc") {
        if (rejects("/sd", "../etc/passwd")) PASS(); else FAIL("");
    }
    TEST("rejects a/../b") {
        if (rejects("/sd", "a/../b")) PASS(); else FAIL("");
    }
    TEST("rejects leading /../") {
        if (rejects("/sd", "/../secret")) PASS(); else FAIL("");
    }
    TEST("rejects overflow") {
        char out[8];
        bool ok = sdResolvePath("/sd", "averylongfilename.txt", out, sizeof(out));
        if (!ok) PASS(); else FAIL("");
    }
    TEST("rejects null args") {
        char out[16];
        if (!sdResolvePath(nullptr, "x", out, sizeof(out)) &&
            !sdResolvePath("/sd", nullptr, out, sizeof(out)))
            PASS(); else FAIL("");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d SD PATH TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d SD PATH TESTS PASSED ===\n", passed, total);
    return 1;
}
