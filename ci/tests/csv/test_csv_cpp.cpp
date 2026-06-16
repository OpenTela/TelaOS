/**
 * Test: C++ CSV library via VFS
 *   csv_parser.h  — parseLine()
 *   csv_escape.h  — escape() / unescape()
 *   csv_io.h      — read<T> / write<T> / append<T> (LittleFS)
 *   mappable.h    — CSV::const_fields_of()
 *
 * All file I/O goes through VFS mock.
 * Delimiter: semicolon (;)
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "csv/csv_io.h"

static int g_passed = 0, g_total = 0;
#define TEST(name)     printf("  %-55s ", name); g_total++;
#define PASS()         do { printf("✓\n"); g_passed++; } while(0)
#define FAIL(msg)      printf("✗ %s\n", msg)
#define FAIL_V(f, ...) do { printf("✗ "); printf(f, __VA_ARGS__); printf("\n"); } while(0)
#define SECTION(name)  printf("\n%s:\n", name)

// ─── Test structs — plain, only fields() ────────────────

struct Score {
    std::string name;
    int score;
    std::string date;
    auto fields() { return std::tie(name, score, date); }
};

struct User {
    std::string login;
    std::string email;
    int age;
    bool premium;
    auto fields() { return std::tie(login, email, age, premium); }
};

struct Product {
    std::string title;
    float price;
    int stock;
    auto fields() { return std::tie(title, price, stock); }
};

struct Message {
    std::string author;
    std::string text;
    auto fields() { return std::tie(author, text); }
};

static const char* TMP = "/data/test.csv";

static void vfs_reset() {
    LittleFS.reset();
    LittleFS.mkdir("/data");
    LittleFS.begin();
}

int main() {
    printf("=== C++ CSV Library Tests (VFS) ===\n");

    // ─── 1. Escape / Unescape ───────────────────────────

    SECTION("1. Escape / Unescape");

    TEST("plain string unchanged") {
        if (CSV::escape("hello") == "hello") PASS();
        else FAIL_V("got '%s'", CSV::escape("hello").c_str());
    }

    TEST("semicolon gets quoted") {
        auto e = CSV::escape("hello; world");
        if (e.find('"') != std::string::npos && e.find(';') != std::string::npos) PASS();
        else FAIL_V("got '%s'", e.c_str());
    }

    TEST("quotes get doubled") {
        auto e = CSV::escape("say \"hi\"");
        if (e.find("\"\"") != std::string::npos) PASS();
        else FAIL_V("got '%s'", e.c_str());
    }

    TEST("unescape plain") {
        if (CSV::unescape("hello") == "hello") PASS();
        else FAIL("mismatch");
    }

    TEST("unescape quoted with semicolon") {
        if (CSV::unescape("\"hello; world\"") == "hello; world") PASS();
        else FAIL_V("got '%s'", CSV::unescape("\"hello; world\"").c_str());
    }

    TEST("unescape doubled quotes") {
        if (CSV::unescape("\"say \"\"hi\"\"\"") == "say \"hi\"") PASS();
        else FAIL_V("got '%s'", CSV::unescape("\"say \"\"hi\"\"\"").c_str());
    }

    // ─── 2. parseLine ───────────────────────────────────

    SECTION("2. parseLine");

    TEST("simple 3 fields") {
        auto f = CSV::parseLine("alice;100;2026-02-08");
        if (f.size() == 3 && f[0] == "alice" && f[1] == "100" && f[2] == "2026-02-08") PASS();
        else FAIL_V("size=%d", (int)f.size());
    }

    TEST("quoted field with semicolon") {
        auto f = CSV::parseLine("\"Bob; Jr.\";200;2026");
        if (f.size() == 3 && f[0] == "Bob; Jr.") PASS();
        else FAIL_V("f[0]='%s'", f.size() > 0 ? f[0].c_str() : "?");
    }

    TEST("quoted field with doubled quotes") {
        auto f = CSV::parseLine("\"Alice \"\"Pro\"\" Smith\";300;2026");
        if (f.size() == 3 && f[0] == "Alice \"Pro\" Smith") PASS();
        else FAIL_V("f[0]='%s'", f.size() > 0 ? f[0].c_str() : "?");
    }

    // ─── 3. Write + Read via VFS ────────────────────────

    SECTION("3. Write + Read via VFS");

    TEST("write 3 scores, read back from VFS") {
        vfs_reset();
        std::vector<Score> scores = {
            {"Alice", 1250, "2026-02-08"},
            {"Bob", 980, "2026-02-07"},
            {"Charlie", 1500, "2026-02-06"}
        };
        CSV::write(TMP, scores);

        // Verify file exists in VFS
        bool exists = LittleFS.exists(TMP);
        auto loaded = CSV::read<Score>(TMP);
        if (exists && loaded.size() == 3 &&
            loaded[0].name == "Alice" && loaded[0].score == 1250 &&
            loaded[2].name == "Charlie" && loaded[2].score == 1500) PASS();
        else FAIL_V("exists=%d size=%d", exists, (int)loaded.size());
    }

    TEST("VFS file contains correct CSV text") {
        vfs_reset();
        std::vector<Score> scores = {{"Alice", 100, "2026"}};
        CSV::write(TMP, scores);

        // Read raw bytes from VFS
        std::string raw = LittleFS.readFile(TMP);
        if (raw.find("Alice;100;2026") != std::string::npos) PASS();
        else FAIL_V("raw='%s'", raw.c_str());
    }

    TEST("roundtrip with special chars in name") {
        vfs_reset();
        std::vector<Score> scores = {
            {"Alice \"Pro\" Smith", 9999, "2026-02-08"},
            {"Bob; Jr.", 1234, "2026-02-07"}
        };
        CSV::write(TMP, scores);
        auto loaded = CSV::read<Score>(TMP);
        if (loaded.size() == 2 &&
            loaded[0].name == "Alice \"Pro\" Smith" &&
            loaded[1].name == "Bob; Jr.") PASS();
        else FAIL_V("n0='%s' n1='%s'",
            loaded.size() > 0 ? loaded[0].name.c_str() : "?",
            loaded.size() > 1 ? loaded[1].name.c_str() : "?");
    }

    // ─── 4. Append via VFS ──────────────────────────────

    SECTION("4. Append via VFS");

    TEST("append adds row to existing VFS file") {
        vfs_reset();
        std::vector<Score> scores = {{"Alice", 100, "2026-01"}};
        CSV::write(TMP, scores);
        CSV::append(TMP, Score{"Bob", 200, "2026-02"});
        auto loaded = CSV::read<Score>(TMP);
        if (loaded.size() == 2 && loaded[1].name == "Bob" && loaded[1].score == 200) PASS();
        else FAIL_V("size=%d", (int)loaded.size());
    }

    TEST("multiple appends accumulate") {
        vfs_reset();
        std::vector<Score> scores = {{"A", 1, "d1"}};
        CSV::write(TMP, scores);
        CSV::append(TMP, Score{"B", 2, "d2"});
        CSV::append(TMP, Score{"C", 3, "d3"});
        auto loaded = CSV::read<Score>(TMP);
        if (loaded.size() == 3 && loaded[2].name == "C") PASS();
        else FAIL_V("size=%d", (int)loaded.size());
    }

    // ─── 5. Multiple types ──────────────────────────────

    SECTION("5. Multiple types");

    TEST("User with bool field") {
        vfs_reset();
        std::vector<User> users = {
            {"alice", "alice@ex.com", 25, true},
            {"bob", "bob@ex.com", 30, false}
        };
        CSV::write(TMP, users);
        auto loaded = CSV::read<User>(TMP);
        if (loaded.size() == 2 &&
            loaded[0].login == "alice" && loaded[0].premium == true &&
            loaded[1].premium == false) PASS();
        else FAIL_V("size=%d", (int)loaded.size());
    }

    TEST("Product with float field") {
        vfs_reset();
        std::vector<Product> products = {
            {"Laptop", 999.99f, 10},
            {"Mouse", 29.99f, 100}
        };
        CSV::write(TMP, products);
        auto loaded = CSV::read<Product>(TMP);
        if (loaded.size() == 2 &&
            loaded[0].title == "Laptop" && loaded[0].price > 999.0f &&
            loaded[1].stock == 100) PASS();
        else FAIL_V("size=%d", (int)loaded.size());
    }

    // ─── 6. Special characters ──────────────────────────

    SECTION("6. Special characters");

    TEST("newline in field") {
        vfs_reset();
        std::vector<Message> msgs = {{"Alice", "Hello\nWorld"}};
        CSV::write(TMP, msgs);
        auto loaded = CSV::read<Message>(TMP);
        if (loaded.size() == 1 && loaded[0].text == "Hello\nWorld") PASS();
        else FAIL_V("size=%d", (int)loaded.size());
    }

    TEST("tab in field") {
        vfs_reset();
        std::vector<Message> msgs = {{"Bob", "Col1\tCol2"}};
        CSV::write(TMP, msgs);
        auto loaded = CSV::read<Message>(TMP);
        if (loaded.size() == 1 && loaded[0].text == "Col1\tCol2") PASS();
        else FAIL("mismatch");
    }

    TEST("backslash in field") {
        vfs_reset();
        std::vector<Message> msgs = {{"Charlie", "path\\to\\file"}};
        CSV::write(TMP, msgs);
        auto loaded = CSV::read<Message>(TMP);
        if (loaded.size() == 1 && loaded[0].text == "path\\to\\file") PASS();
        else FAIL_V("text='%s'", loaded.size() > 0 ? loaded[0].text.c_str() : "?");
    }

    // ─── 7. const_fields_of ─────────────────────────────

    SECTION("7. const_fields_of");

    TEST("works on const object") {
        const Score cs{"Const", 7, "2026"};
        auto cf = CSV::const_fields_of(cs);
        if (std::get<0>(cf) == "Const" && std::get<1>(cf) == 7) PASS();
        else FAIL("mismatch");
    }

    TEST("works on non-const object") {
        Score s{"Alice", 100, "2026"};
        auto cf = CSV::const_fields_of(s);
        if (std::get<0>(cf) == "Alice" && std::get<1>(cf) == 100) PASS();
        else FAIL("mismatch");
    }

    TEST("fields() returns writable refs") {
        Score s{"Before", 0, "2026"};
        auto f = s.fields();
        std::get<0>(f) = "After";
        std::get<1>(f) = 99;
        if (s.name == "After" && s.score == 99) PASS();
        else FAIL_V("name='%s' score=%d", s.name.c_str(), s.score);
    }

    // ─── 8. Edge cases ──────────────────────────────────

    SECTION("8. Edge cases");

    TEST("read nonexistent file → empty vector") {
        vfs_reset();
        auto loaded = CSV::read<Score>("/data/nope.csv");
        if (loaded.empty()) PASS();
        else FAIL_V("size=%d", (int)loaded.size());
    }

    TEST("write to new path creates file in VFS") {
        vfs_reset();
        CSV::write("/data/new.csv", std::vector<Score>{{"X", 1, "d"}});
        if (LittleFS.exists("/data/new.csv")) PASS();
        else FAIL("file not in VFS");
    }

    // ═════════════════════════════════════════════════════

    printf("\n────────────────────────────────────────────────────\n");
    if (g_passed == g_total) {
        printf("  ✓ ALL %d C++ CSV TESTS PASSED\n", g_total);
    } else {
        printf("  ✗ %d/%d PASSED, %d FAILED\n", g_passed, g_total, g_total - g_passed);
    }
    printf("────────────────────────────────────────────────────\n");
    return g_passed == g_total ? 0 : 1;
}
