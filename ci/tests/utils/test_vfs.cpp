/**
 * Test: Virtual File System
 * In-memory LittleFS replacement — read/write, dirs, listing
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <LittleFS.h>

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== VFS Tests ===\n\n");
    int passed = 0, total = 0;

    // === Write / Read ===
    printf("Write/Read:\n");

    TEST("writeFile + read back") {
        LittleFS.reset();
        LittleFS.writeFile("/test.txt", "hello world");
        File f = LittleFS.open("/test.txt", "r");
        char buf[64] = {};
        f.readBytes(buf, f.size());
        f.close();
        if (strcmp(buf, "hello world") == 0) PASS(); else FAIL(buf);
    }

    TEST("file size correct") {
        LittleFS.reset();
        LittleFS.writeFile("/data.bin", "12345");
        File f = LittleFS.open("/data.bin");
        if (f.size() == 5) PASS(); else { FAIL(""); printf("      size=%zu\n", f.size()); }
        f.close();
    }

    TEST("partial read") {
        LittleFS.reset();
        LittleFS.writeFile("/big.txt", "ABCDEFGHIJ");
        File f = LittleFS.open("/big.txt");
        char buf[4] = {};
        size_t n = f.readBytes(buf, 3);
        if (n == 3 && strncmp(buf, "ABC", 3) == 0) PASS();
        else { FAIL(""); printf("      n=%zu buf='%s'\n", n, buf); }
        f.close();
    }

    TEST("sequential reads advance position") {
        LittleFS.reset();
        LittleFS.writeFile("/seq.txt", "ABCDEF");
        File f = LittleFS.open("/seq.txt");
        char a[4] = {}, b[4] = {};
        f.readBytes(a, 3);
        f.readBytes(b, 3);
        f.close();
        if (strncmp(a, "ABC", 3) == 0 && strncmp(b, "DEF", 3) == 0) PASS();
        else { FAIL(""); printf("      a='%s' b='%s'\n", a, b); }
    }

    TEST("available() tracks remaining") {
        LittleFS.reset();
        LittleFS.writeFile("/av.txt", "XY");
        File f = LittleFS.open("/av.txt");
        bool before = f.available();
        char buf[4];
        f.readBytes(buf, 2);
        bool after = f.available();
        f.close();
        if (before && !after) PASS(); else FAIL("");
    }

    // === Write via File ===
    printf("\nFile write:\n");

    TEST("open for write + close persists") {
        LittleFS.reset();
        File f = LittleFS.open("/out.txt", "w");
        f.print("written content");
        f.close();
        // Read back
        File r = LittleFS.open("/out.txt", "r");
        char buf[64] = {};
        r.readBytes(buf, r.size());
        r.close();
        if (strcmp(buf, "written content") == 0) PASS(); else FAIL(buf);
    }

    TEST("println adds newline") {
        LittleFS.reset();
        File f = LittleFS.open("/nl.txt", "w");
        f.println("line1");
        f.println("line2");
        f.close();
        File r = LittleFS.open("/nl.txt");
        char buf[64] = {};
        r.readBytes(buf, r.size());
        r.close();
        if (strcmp(buf, "line1\nline2\n") == 0) PASS(); else FAIL(buf);
    }

    // === Exists ===
    printf("\nExists:\n");

    TEST("exists returns true for file") {
        LittleFS.reset();
        LittleFS.writeFile("/a.txt", "data");
        if (LittleFS.exists("/a.txt")) PASS(); else FAIL("");
    }

    TEST("exists returns false for missing") {
        LittleFS.reset();
        if (!LittleFS.exists("/nope.txt")) PASS(); else FAIL("");
    }

    TEST("exists returns true for dir") {
        LittleFS.reset();
        LittleFS.mkdir("/mydir");
        if (LittleFS.exists("/mydir")) PASS(); else FAIL("");
    }

    // === Open missing file ===
    printf("\nMissing:\n");

    TEST("open missing file returns invalid File") {
        LittleFS.reset();
        File f = LittleFS.open("/missing.txt");
        if (!f) PASS(); else FAIL("should be invalid");
    }

    // === Directories ===
    printf("\nDirectories:\n");

    TEST("mkdir + exists") {
        LittleFS.reset();
        LittleFS.mkdir("/data");
        if (LittleFS.exists("/data")) PASS(); else FAIL("");
    }

    TEST("auto-create parent dirs from writeFile") {
        LittleFS.reset();
        LittleFS.writeFile("/a/b/c.txt", "deep");
        if (LittleFS.exists("/a") && LittleFS.exists("/a/b")) PASS(); else FAIL("");
    }

    TEST("open dir returns directory File") {
        LittleFS.reset();
        LittleFS.mkdir("/stuff");
        LittleFS.writeFile("/stuff/one.txt", "1");
        LittleFS.writeFile("/stuff/two.txt", "2");
        File dir = LittleFS.open("/stuff");
        if (dir && dir.isDirectory()) PASS(); else FAIL("not directory");
    }

    TEST("openNextFile iterates entries") {
        LittleFS.reset();
        LittleFS.mkdir("/items");
        LittleFS.writeFile("/items/a.txt", "A");
        LittleFS.writeFile("/items/b.txt", "B");
        File dir = LittleFS.open("/items");
        int count = 0;
        File entry;
        while ((entry = dir.openNextFile())) {
            count++;
            entry.close();
        }
        if (count == 2) PASS();
        else { FAIL(""); printf("      count=%d\n", count); }
    }

    TEST("openNextFile returns invalid after last") {
        LittleFS.reset();
        LittleFS.mkdir("/one");
        LittleFS.writeFile("/one/f.txt", "x");
        File dir = LittleFS.open("/one");
        dir.openNextFile(); // consume
        File end = dir.openNextFile();
        if (!end) PASS(); else FAIL("should be invalid");
    }

    TEST("file name() returns basename") {
        LittleFS.reset();
        LittleFS.writeFile("/dir/myfile.txt", "data");
        File f = LittleFS.open("/dir/myfile.txt");
        if (f && strcmp(f.name(), "myfile.txt") == 0) PASS();
        else { FAIL(f ? f.name() : "null"); }
        f.close();
    }

    TEST("file path() returns full path") {
        LittleFS.reset();
        LittleFS.writeFile("/a/b.txt", "x");
        File f = LittleFS.open("/a/b.txt");
        if (f && strcmp(f.path(), "/a/b.txt") == 0) PASS();
        else { FAIL(f ? f.path() : "null"); }
        f.close();
    }

    // === Remove ===
    printf("\nRemove:\n");

    TEST("remove deletes file") {
        LittleFS.reset();
        LittleFS.writeFile("/del.txt", "gone");
        LittleFS.remove("/del.txt");
        if (!LittleFS.exists("/del.txt")) PASS(); else FAIL("still exists");
    }

    TEST("rmdir deletes directory") {
        LittleFS.reset();
        LittleFS.mkdir("/rmme");
        LittleFS.rmdir("/rmme");
        if (!LittleFS.exists("/rmme")) PASS(); else FAIL("still exists");
    }

    // === usedBytes ===
    printf("\nUsedBytes:\n");

    TEST("usedBytes tracks file sizes") {
        LittleFS.reset();
        LittleFS.writeFile("/a.txt", "12345");     // 5
        LittleFS.writeFile("/b.txt", "1234567890"); // 10
        size_t used = LittleFS.usedBytes();
        if (used == 15) PASS();
        else { FAIL(""); printf("      used=%zu\n", used); }
    }

    // === begin ===
    printf("\nBegin:\n");

    TEST("begin returns true") {
        if (LittleFS.begin(true)) PASS(); else FAIL("");
    }

    // === reset ===
    printf("\nReset:\n");

    TEST("LittleFS.reset clears everything") {
        LittleFS.writeFile("/x.txt", "data");
        LittleFS.mkdir("/dir");
        LittleFS.reset();
        if (!LittleFS.exists("/x.txt") && !LittleFS.exists("/dir")) PASS();
        else FAIL("not cleared");
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d VFS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d VFS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
