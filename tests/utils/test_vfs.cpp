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
        VFS::reset();
        VFS::writeFile("/test.txt", "hello world");
        File f = LittleFS.open("/test.txt", "r");
        char buf[64] = {};
        f.readBytes(buf, f.size());
        f.close();
        if (strcmp(buf, "hello world") == 0) PASS(); else FAIL(buf);
    }

    TEST("file size correct") {
        VFS::reset();
        VFS::writeFile("/data.bin", "12345");
        File f = LittleFS.open("/data.bin");
        if (f.size() == 5) PASS(); else { FAIL(""); printf("      size=%zu\n", f.size()); }
        f.close();
    }

    TEST("partial read") {
        VFS::reset();
        VFS::writeFile("/big.txt", "ABCDEFGHIJ");
        File f = LittleFS.open("/big.txt");
        char buf[4] = {};
        size_t n = f.readBytes(buf, 3);
        if (n == 3 && strncmp(buf, "ABC", 3) == 0) PASS();
        else { FAIL(""); printf("      n=%zu buf='%s'\n", n, buf); }
        f.close();
    }

    TEST("sequential reads advance position") {
        VFS::reset();
        VFS::writeFile("/seq.txt", "ABCDEF");
        File f = LittleFS.open("/seq.txt");
        char a[4] = {}, b[4] = {};
        f.readBytes(a, 3);
        f.readBytes(b, 3);
        f.close();
        if (strncmp(a, "ABC", 3) == 0 && strncmp(b, "DEF", 3) == 0) PASS();
        else { FAIL(""); printf("      a='%s' b='%s'\n", a, b); }
    }

    TEST("available() tracks remaining") {
        VFS::reset();
        VFS::writeFile("/av.txt", "XY");
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
        VFS::reset();
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
        VFS::reset();
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
        VFS::reset();
        VFS::writeFile("/a.txt", "data");
        if (LittleFS.exists("/a.txt")) PASS(); else FAIL("");
    }

    TEST("exists returns false for missing") {
        VFS::reset();
        if (!LittleFS.exists("/nope.txt")) PASS(); else FAIL("");
    }

    TEST("exists returns true for dir") {
        VFS::reset();
        VFS::mkdir("/mydir");
        if (LittleFS.exists("/mydir")) PASS(); else FAIL("");
    }

    // === Open missing file ===
    printf("\nMissing:\n");

    TEST("open missing file returns invalid File") {
        VFS::reset();
        File f = LittleFS.open("/missing.txt");
        if (!f) PASS(); else FAIL("should be invalid");
    }

    // === Directories ===
    printf("\nDirectories:\n");

    TEST("mkdir + exists") {
        VFS::reset();
        LittleFS.mkdir("/data");
        if (LittleFS.exists("/data")) PASS(); else FAIL("");
    }

    TEST("auto-create parent dirs from writeFile") {
        VFS::reset();
        VFS::writeFile("/a/b/c.txt", "deep");
        if (VFS::exists("/a") && VFS::exists("/a/b")) PASS(); else FAIL("");
    }

    TEST("open dir returns directory File") {
        VFS::reset();
        VFS::mkdir("/stuff");
        VFS::writeFile("/stuff/one.txt", "1");
        VFS::writeFile("/stuff/two.txt", "2");
        File dir = LittleFS.open("/stuff");
        if (dir && dir.isDirectory()) PASS(); else FAIL("not directory");
    }

    TEST("openNextFile iterates entries") {
        VFS::reset();
        VFS::mkdir("/items");
        VFS::writeFile("/items/a.txt", "A");
        VFS::writeFile("/items/b.txt", "B");
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
        VFS::reset();
        VFS::mkdir("/one");
        VFS::writeFile("/one/f.txt", "x");
        File dir = LittleFS.open("/one");
        dir.openNextFile(); // consume
        File end = dir.openNextFile();
        if (!end) PASS(); else FAIL("should be invalid");
    }

    TEST("file name() returns basename") {
        VFS::reset();
        VFS::writeFile("/dir/myfile.txt", "data");
        File f = LittleFS.open("/dir/myfile.txt");
        if (f && strcmp(f.name(), "myfile.txt") == 0) PASS();
        else { FAIL(f ? f.name() : "null"); }
        f.close();
    }

    TEST("file path() returns full path") {
        VFS::reset();
        VFS::writeFile("/a/b.txt", "x");
        File f = LittleFS.open("/a/b.txt");
        if (f && strcmp(f.path(), "/a/b.txt") == 0) PASS();
        else { FAIL(f ? f.path() : "null"); }
        f.close();
    }

    // === Remove ===
    printf("\nRemove:\n");

    TEST("remove deletes file") {
        VFS::reset();
        VFS::writeFile("/del.txt", "gone");
        LittleFS.remove("/del.txt");
        if (!LittleFS.exists("/del.txt")) PASS(); else FAIL("still exists");
    }

    TEST("rmdir deletes directory") {
        VFS::reset();
        VFS::mkdir("/rmme");
        LittleFS.rmdir("/rmme");
        if (!LittleFS.exists("/rmme")) PASS(); else FAIL("still exists");
    }

    // === usedBytes ===
    printf("\nUsedBytes:\n");

    TEST("usedBytes tracks file sizes") {
        VFS::reset();
        VFS::writeFile("/a.txt", "12345");     // 5
        VFS::writeFile("/b.txt", "1234567890"); // 10
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

    TEST("VFS::reset clears everything") {
        VFS::writeFile("/x.txt", "data");
        VFS::mkdir("/dir");
        VFS::reset();
        if (!VFS::exists("/x.txt") && !VFS::exists("/dir")) PASS();
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
