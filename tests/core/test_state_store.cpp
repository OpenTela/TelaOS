/**
 * Test: State Store
 * All variable types, define/get/set, reset, onChange, type conversion
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== State Store Tests ===\n\n");
    int passed = 0, total = 0;

    // === String type ===
    printf("String:\n");

    TEST("defineString + getString") {
        Store s("test");
        s.defineString("name", "Alice");
        if (s.getString("name") == "Alice") PASS(); else FAIL(s.getString("name").c_str());
    }

    TEST("setString changes value") {
        Store s("test");
        s.defineString("name", "Alice");
        s.setString("name", "Bob");
        if (s.getString("name") == "Bob") PASS(); else FAIL(s.getString("name").c_str());
    }

    TEST("undefined var returns empty") {
        Store s("test");
        if (s.getString("missing").empty()) PASS(); else FAIL("not empty");
    }

    // === Int type ===
    printf("\nInt:\n");

    TEST("defineInt + getInt") {
        Store s("test");
        s.defineInt("count", 42);
        if (s.getInt("count") == 42) PASS(); else { FAIL(""); printf("      got %d\n", s.getInt("count")); }
    }

    TEST("setInt changes value") {
        Store s("test");
        s.defineInt("count", 0);
        s.setInt("count", 99);
        if (s.getInt("count") == 99) PASS(); else { FAIL(""); printf("      got %d\n", s.getInt("count")); }
    }

    TEST("getInt from string var") {
        Store s("test");
        s.defineString("num", "123");
        if (s.getInt("num") == 123) PASS(); else { FAIL(""); printf("      got %d\n", s.getInt("num")); }
    }

    // === Bool type ===
    printf("\nBool:\n");

    TEST("defineBool + getBool") {
        Store s("test");
        s.defineBool("flag", true);
        if (s.getBool("flag") == true) PASS(); else FAIL("false");
    }

    TEST("setBool false") {
        Store s("test");
        s.defineBool("flag", true);
        s.setBool("flag", false);
        if (s.getBool("flag") == false) PASS(); else FAIL("true");
    }

    TEST("getBool from string 'true'") {
        Store s("test");
        s.defineString("flag", "true");
        if (s.getBool("flag") == true) PASS(); else FAIL("false");
    }

    TEST("getBool from string '0'") {
        Store s("test");
        s.defineString("flag", "0");
        if (s.getBool("flag") == false) PASS(); else FAIL("true");
    }

    // === Float type ===
    printf("\nFloat:\n");

    TEST("defineFloat + getFloat") {
        Store s("test");
        s.defineFloat("temp", 22.5f);
        float v = s.getFloat("temp");
        if (v > 22.4f && v < 22.6f) PASS(); else { FAIL(""); printf("      got %.2f\n", v); }
    }

    TEST("getFloat from string") {
        Store s("test");
        s.defineString("val", "3.14");
        float v = s.getFloat("val");
        if (v > 3.13f && v < 3.15f) PASS(); else { FAIL(""); printf("      got %.2f\n", v); }
    }

    // === has / count ===
    printf("\nhas/count:\n");

    TEST("has returns true for defined") {
        Store s("test");
        s.defineString("x", "");
        if (s.has("x")) PASS(); else FAIL("");
    }

    TEST("has returns false for undefined") {
        Store s("test");
        if (!s.has("missing")) PASS(); else FAIL("");
    }

    TEST("count tracks vars") {
        Store s("test");
        s.defineString("a", "");
        s.defineInt("b", 0);
        s.defineBool("c", false);
        if (s.count() == 3) PASS(); else { FAIL(""); printf("      got %zu\n", s.count()); }
    }

    // === getAsString ===
    printf("\ngetAsString:\n");

    TEST("int → string") {
        Store s("test");
        s.defineInt("n", 42);
        if (s.getAsString("n") == "42") PASS(); else FAIL(s.getAsString("n").c_str());
    }

    TEST("bool true → 'true'") {
        Store s("test");
        s.defineBool("f", true);
        if (s.getAsString("f") == "true") PASS(); else FAIL(s.getAsString("f").c_str());
    }

    TEST("bool false → 'false'") {
        Store s("test");
        s.defineBool("f", false);
        if (s.getAsString("f") == "false") PASS(); else FAIL(s.getAsString("f").c_str());
    }

    // === setFromString (type conversion) ===
    printf("\nsetFromString:\n");

    TEST("setFromString int") {
        Store s("test");
        s.defineInt("n", 0);
        s.setFromString("n", "77");
        if (s.getInt("n") == 77) PASS(); else { FAIL(""); printf("      got %d\n", s.getInt("n")); }
    }

    TEST("setFromString bool") {
        Store s("test");
        s.defineBool("f", false);
        s.setFromString("f", "true");
        if (s.getBool("f") == true) PASS(); else FAIL("");
    }

    TEST("setFromString float") {
        Store s("test");
        s.defineFloat("v", 0.0f);
        s.setFromString("v", "2.71");
        float f = s.getFloat("v");
        if (f > 2.70f && f < 2.72f) PASS(); else { FAIL(""); printf("      got %.2f\n", f); }
    }

    TEST("setFromString undefined → creates string") {
        Store s("test");
        s.setFromString("newvar", "hello");
        if (s.getString("newvar") == "hello") PASS(); else FAIL(s.getString("newvar").c_str());
    }

    // === reset ===
    printf("\nreset:\n");

    TEST("reset restores default") {
        Store s("test");
        s.defineInt("n", 10);
        s.setInt("n", 99);
        s.reset("n");
        if (s.getInt("n") == 10) PASS(); else { FAIL(""); printf("      got %d\n", s.getInt("n")); }
    }

    TEST("resetAll restores all defaults") {
        Store s("test");
        s.defineString("a", "X");
        s.defineInt("b", 5);
        s.setString("a", "Y");
        s.setInt("b", 99);
        s.resetAll();
        if (s.getString("a") == "X" && s.getInt("b") == 5) PASS();
        else FAIL("");
    }

    TEST("clear removes all vars") {
        Store s("test");
        s.defineString("a", "");
        s.defineInt("b", 0);
        s.clear();
        if (s.count() == 0 && !s.has("a")) PASS(); else FAIL("");
    }

    // === onChange callback ===
    printf("\nonChange:\n");

    TEST("onChange fires on set") {
        Store s("test");
        s.defineString("x", "old");
        std::string changed;
        s.onChange([&](const P::String& name, const VarValue&) { changed = name.c_str(); });
        s.setString("x", "new");
        if (changed == "x") PASS(); else FAIL(changed.c_str());
    }

    TEST("onChange not fired when value same") {
        Store s("test");
        s.defineString("x", "same");
        int count = 0;
        s.onChange([&](const P::String&, const VarValue&) { count++; });
        s.setString("x", "same");
        if (count == 0) PASS(); else { FAIL(""); printf("      fired %d times\n", count); }
    }

    TEST("onChange suppressed with notify=false") {
        Store s("test");
        s.defineString("x", "old");
        int count = 0;
        s.onChange([&](const P::String&, const VarValue&) { count++; });
        s.setString("x", "new", false);
        if (count == 0) PASS(); else { FAIL(""); printf("      fired %d times\n", count); }
    }

    // === index access ===
    printf("\nindex access:\n");

    TEST("nameAt preserves order") {
        Store s("test");
        s.defineString("first", "");
        s.defineString("second", "");
        s.defineString("third", "");
        if (s.nameAt(0) == "first" && s.nameAt(1) == "second" && s.nameAt(2) == "third") PASS();
        else FAIL("");
    }

    TEST("typeAt returns correct type") {
        Store s("test");
        s.defineString("a", "");
        s.defineInt("b", 0);
        s.defineBool("c", false);
        if (s.typeAt(0) == VarType::String && s.typeAt(1) == VarType::Int && s.typeAt(2) == VarType::Bool)
            PASS(); else FAIL("");
    }

    // === type helpers ===
    printf("\ntype helpers:\n");

    TEST("typeToString") {
        if (strcmp(Store::typeToString(VarType::String), "string") == 0 &&
            strcmp(Store::typeToString(VarType::Int), "int") == 0 &&
            strcmp(Store::typeToString(VarType::Bool), "bool") == 0 &&
            strcmp(Store::typeToString(VarType::Float), "float") == 0)
            PASS(); else FAIL("");
    }

    TEST("stringToType") {
        if (Store::stringToType("string") == VarType::String &&
            Store::stringToType("int") == VarType::Int &&
            Store::stringToType("bool") == VarType::Bool &&
            Store::stringToType("float") == VarType::Float)
            PASS(); else FAIL("");
    }

    TEST("isKnownType") {
        if (Store::isKnownType("int") && Store::isKnownType("string") &&
            !Store::isKnownType("unknown") && !Store::isKnownType(""))
            PASS(); else FAIL("");
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d STATE STORE TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d STATE STORE TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
