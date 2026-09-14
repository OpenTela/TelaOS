/**
 * test_xml_utils.cpp — Unit tests for XML utility functions
 *
 * Tests findTagOpen, findTagClose, getAttr, getAttrInt, getAttrBool
 */

#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/xml_utils.h"

using namespace UI::XmlUtils;

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

int main() {
    printf("=== XML UTILS TESTS ===\n\n");
    int passed = 0, total = 0;

    // === findTagOpen ===
    printf("findTagOpen:\n");

    TEST("find <page in simple doc") {
        const char* html = "<app><page id=\"main\">content</page></app>";
        const char* p = findTagOpen(html, "page");
        if (p && strncmp(p, "<page", 5) == 0) PASS();
        else FAIL(p ? "wrong position" : "not found");
    }

    TEST("find <button in nested") {
        const char* html = "<page><label>hi</label><button id=\"b\">X</button></page>";
        const char* p = findTagOpen(html, "button");
        if (p && strncmp(p, "<button", 7) == 0) PASS();
        else FAIL(p ? "wrong position" : "not found");
    }

    TEST("returns nullptr for missing tag") {
        const char* html = "<page><label>hi</label></page>";
        const char* p = findTagOpen(html, "button");
        if (p == nullptr) PASS();
        else FAIL("should be null");
    }

    TEST("does not match partial: <pages != <page") {
        const char* html = "<pages>content</pages>";
        const char* p = findTagOpen(html, "page");
        if (p == nullptr) PASS();
        else FAIL("should not match <pages");
    }

    TEST("matches self-closing <input/>") {
        const char* html = "<page><input/></page>";
        const char* p = findTagOpen(html, "input");
        if (p && strncmp(p, "<input", 6) == 0) PASS();
        else FAIL(p ? "wrong" : "not found");
    }

    // === findTagClose ===
    printf("\nfindTagClose:\n");

    TEST("find </page> simple") {
        const char* html = "content here</page>rest";
        const char* p = findTagClose(html, "page");
        if (p && strncmp(p, "</page>", 7) == 0) PASS();
        else FAIL(p ? "wrong position" : "not found");
    }

    TEST("handles nested same tags") {
        // Starts at depth=1. <page> bumps to 2. First </page> goes to 1. Second </page> goes to 0.
        const char* html = "<page>inner</page>outer</page>rest";
        const char* p = findTagClose(html, "page");
        // Should return at second </page> (after "outer")
        if (p && strncmp(p, "</page>rest", 11) == 0) PASS();
        else FAIL(p ? "wrong depth" : "not found");
    }

    TEST("returns nullptr if no closing tag") {
        const char* html = "content without closing tag";
        const char* p = findTagClose(html, "page");
        if (p == nullptr) PASS();
        else FAIL("should be null");
    }

    TEST("works with large content (>10000 chars)") {
        // Build a string longer than old MAX_ITERATIONS
        std::string big(15000, 'x');
        big += "</page>";
        const char* p = findTagClose(big.c_str(), "page");
        if (p && strncmp(p, "</page>", 7) == 0) PASS();
        else FAIL(p ? "wrong" : "not found — old limit bug?");
    }

    // === getAttr ===
    printf("\ngetAttr:\n");

    {
        const char* attrs = " id=\"main\" x=\"10%\" bgcolor=\"#fff\" visible=\"{show}\">";
        const char* end = attrs + strlen(attrs) - 1; // before >

        TEST("id = \"main\"") {
            auto val = getAttr(attrs, end, "id");
            if (val == "main") PASS();
            else FAIL(val.c_str());
        }

        TEST("x = \"10%\"") {
            auto val = getAttr(attrs, end, "x");
            if (val == "10%") PASS();
            else FAIL(val.c_str());
        }

        TEST("bgcolor = \"#fff\"") {
            auto val = getAttr(attrs, end, "bgcolor");
            if (val == "#fff") PASS();
            else FAIL(val.c_str());
        }

        TEST("visible = \"{show}\"") {
            auto val = getAttr(attrs, end, "visible");
            if (val == "{show}") PASS();
            else FAIL(val.c_str());
        }

        TEST("missing attr returns empty") {
            auto val = getAttr(attrs, end, "onclick");
            if (val.empty()) PASS();
            else FAIL(val.c_str());
        }
    }

    // === getAttrInt ===
    printf("\ngetAttrInt:\n");

    {
        const char* attrs = " min=\"0\" max=\"100\" font=\"32\">";
        const char* end = attrs + strlen(attrs) - 1;

        TEST("min = 0") {
            int val = getAttrInt(attrs, end, "min");
            if (val == 0) PASS();
            else { char buf[32]; snprintf(buf, sizeof(buf), "got %d", val); FAIL(buf); }
        }

        TEST("max = 100") {
            int val = getAttrInt(attrs, end, "max");
            if (val == 100) PASS();
            else { char buf[32]; snprintf(buf, sizeof(buf), "got %d", val); FAIL(buf); }
        }

        TEST("font = 32") {
            int val = getAttrInt(attrs, end, "font");
            if (val == 32) PASS();
            else { char buf[32]; snprintf(buf, sizeof(buf), "got %d", val); FAIL(buf); }
        }

        TEST("missing attr returns default") {
            int val = getAttrInt(attrs, end, "width", 42);
            if (val == 42) PASS();
            else { char buf[32]; snprintf(buf, sizeof(buf), "got %d", val); FAIL(buf); }
        }
    }

    // === getAttrBool ===
    printf("\ngetAttrBool:\n");

    {
        const char* attrs = " password=\"true\" readonly=\"false\" hidden=\"1\">";
        const char* end = attrs + strlen(attrs) - 1;

        TEST("password = true") {
            if (getAttrBool(attrs, end, "password")) PASS();
            else FAIL("expected true");
        }

        TEST("readonly = false") {
            if (!getAttrBool(attrs, end, "readonly")) PASS();
            else FAIL("expected false");
        }

        TEST("hidden = 1 → true") {
            if (getAttrBool(attrs, end, "hidden")) PASS();
            else FAIL("expected true for '1'");
        }

        TEST("missing attr returns default false") {
            if (!getAttrBool(attrs, end, "checked", false)) PASS();
            else FAIL("expected default false");
        }

        TEST("missing attr returns default true") {
            if (getAttrBool(attrs, end, "checked", true)) PASS();
            else FAIL("expected default true");
        }
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d XML UTILS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d XML UTILS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
