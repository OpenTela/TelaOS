/**
 * Test: String Utilities
 * Pure function tests — no UI, no Lua, no LVGL needed.
 */
#include <cstdio>
#include <cstring>
#include "utils/string_utils.h"

using namespace UI::StringUtils;

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== String Utils Tests ===\n\n");
    int passed = 0, total = 0;

    // === trim ===
    printf("trim:\n");

    TEST("trim leading spaces") {
        P::String s = "  hello";
        trim(s);
        if (s == "hello") PASS(); else FAIL(s.c_str());
    }

    TEST("trim trailing spaces") {
        P::String s = "hello  ";
        trim(s);
        if (s == "hello") PASS(); else FAIL(s.c_str());
    }

    TEST("trim both sides") {
        P::String s = "  hello  ";
        trim(s);
        if (s == "hello") PASS(); else FAIL(s.c_str());
    }

    TEST("trim tabs and newlines") {
        P::String s = "\t\nhello\r\n";
        trim(s);
        if (s == "hello") PASS(); else FAIL(s.c_str());
    }

    TEST("trim empty string") {
        P::String s = "";
        trim(s);
        if (s.empty()) PASS(); else FAIL(s.c_str());
    }

    TEST("trim all whitespace") {
        P::String s = "   ";
        trim(s);
        if (s.empty()) PASS(); else FAIL(s.c_str());
    }

    TEST("trim no-op") {
        P::String s = "hello";
        trim(s);
        if (s == "hello") PASS(); else FAIL(s.c_str());
    }

    // === trimmed ===
    printf("\ntrimmed:\n");

    TEST("trimmed returns new string") {
        auto r = trimmed("  hi  ");
        if (r == "hi") PASS(); else FAIL(r.c_str());
    }

    TEST("trimmed empty") {
        auto r = trimmed("");
        if (r.empty()) PASS(); else FAIL(r.c_str());
    }

    // === toLower ===
    printf("\ntoLower:\n");

    TEST("toLower basic") {
        P::String s = "Hello World";
        toLower(s);
        if (s == "hello world") PASS(); else FAIL(s.c_str());
    }

    TEST("toLower already lower") {
        P::String s = "abc";
        toLower(s);
        if (s == "abc") PASS(); else FAIL(s.c_str());
    }

    TEST("toLowerCopy") {
        auto r = toLowerCopy("ABC");
        if (r == "abc") PASS(); else FAIL(r.c_str());
    }

    // === contains ===
    printf("\ncontains:\n");

    TEST("contains char — found") {
        if (contains("hello", 'l')) PASS(); else FAIL("");
    }

    TEST("contains char — not found") {
        if (!contains("hello", 'z')) PASS(); else FAIL("");
    }

    TEST("contains string — found") {
        if (contains("hello world", "world")) PASS(); else FAIL("");
    }

    TEST("contains string — not found") {
        if (!contains("hello world", "xyz")) PASS(); else FAIL("");
    }

    TEST("contains empty needle") {
        if (contains("hello", "")) PASS(); else FAIL("");
    }

    // === toBool ===
    printf("\ntoBool:\n");

    TEST("toBool 'true'")  { if (toBool("true"))  PASS(); else FAIL(""); }
    TEST("toBool 'True'")  { if (toBool("True"))  PASS(); else FAIL(""); }
    TEST("toBool '1'")     { if (toBool("1"))     PASS(); else FAIL(""); }
    TEST("toBool 'false'") { if (!toBool("false")) PASS(); else FAIL(""); }
    TEST("toBool '0'")     { if (!toBool("0"))     PASS(); else FAIL(""); }
    TEST("toBool empty")   { if (!toBool(""))      PASS(); else FAIL(""); }
    TEST("toBool nullptr")  { if (!toBool((const char*)nullptr)) PASS(); else FAIL(""); }

    // === equals ===
    printf("\nequals:\n");

    TEST("equals same") {
        if (equals("abc", "abc")) PASS(); else FAIL("");
    }

    TEST("equals different") {
        if (!equals("abc", "def")) PASS(); else FAIL("");
    }

    TEST("equals nullptr") {
        if (!equals(nullptr, "abc")) PASS(); else FAIL("");
    }

    TEST("equalsIgnoreCase match") {
        if (equalsIgnoreCase("Hello", "hELLO")) PASS(); else FAIL("");
    }

    TEST("equalsIgnoreCase no match") {
        if (!equalsIgnoreCase("Hello", "World")) PASS(); else FAIL("");
    }

    TEST("equalsIgnoreCase different length") {
        if (!equalsIgnoreCase("Hi", "Hello")) PASS(); else FAIL("");
    }

    // === decodeEntities ===
    printf("\ndecodeEntities:\n");

    TEST("&lt; &gt;") {
        auto r = decodeEntities("&lt;div&gt;");
        if (r == "<div>") PASS(); else FAIL(r.c_str());
    }

    TEST("&amp;") {
        auto r = decodeEntities("a&amp;b");
        if (r == "a&b") PASS(); else FAIL(r.c_str());
    }

    TEST("&quot; &apos;") {
        auto r = decodeEntities("&quot;hi&apos;");
        if (r == "\"hi'") PASS(); else FAIL(r.c_str());
    }

    TEST("&nbsp;") {
        auto r = decodeEntities("a&nbsp;b");
        if (r == "a b") PASS(); else FAIL(r.c_str());
    }

    TEST("&#65; (decimal A)") {
        auto r = decodeEntities("&#65;");
        if (r == "A") PASS(); else FAIL(r.c_str());
    }

    TEST("&#x41; (hex A)") {
        auto r = decodeEntities("&#x41;");
        if (r == "A") PASS(); else FAIL(r.c_str());
    }

    TEST("no entities — passthrough") {
        auto r = decodeEntities("hello world");
        if (r == "hello world") PASS(); else FAIL(r.c_str());
    }

    TEST("mixed text and entities") {
        auto r = decodeEntities("1 &lt; 2 &amp; 3 &gt; 0");
        if (r == "1 < 2 & 3 > 0") PASS(); else FAIL(r.c_str());
    }

    TEST("unknown entity — kept as is") {
        auto r = decodeEntities("&unknown;");
        if (r == "&unknown;") PASS(); else FAIL(r.c_str());
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d STRING UTILS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d STRING UTILS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
