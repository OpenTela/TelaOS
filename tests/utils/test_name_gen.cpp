/**
 * Test: Name Generator & Sanitizer
 * UTF-8 transliteration, slug generation, pronounceable names
 */
#include <cstdio>
#include <cstring>
#include "utils/name_gen.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

int main() {
    printf("=== Name Gen Tests ===\n\n");
    int passed = 0, total = 0;

    char buf[64];

    // === ASCII ===
    printf("ASCII:\n");

    TEST("lowercase passthrough") {
        NameGen::sanitize(buf, "hello", sizeof(buf));
        if (strcmp(buf, "hello") == 0) PASS(); else FAIL(buf);
    }

    TEST("uppercase → lowercase") {
        NameGen::sanitize(buf, "Hello World", sizeof(buf));
        if (strcmp(buf, "hello-world") == 0) PASS(); else FAIL(buf);
    }

    TEST("spaces → dashes") {
        NameGen::sanitize(buf, "my cool app", sizeof(buf));
        if (strcmp(buf, "my-cool-app") == 0) PASS(); else FAIL(buf);
    }

    TEST("multiple spaces → single dash") {
        NameGen::sanitize(buf, "a   b", sizeof(buf));
        if (strcmp(buf, "a-b") == 0) PASS(); else FAIL(buf);
    }

    TEST("digits preserved") {
        NameGen::sanitize(buf, "app2048", sizeof(buf));
        if (strcmp(buf, "app2048") == 0) PASS(); else FAIL(buf);
    }

    TEST("special chars stripped") {
        NameGen::sanitize(buf, "hello! @world#", sizeof(buf));
        if (strcmp(buf, "hello-world") == 0) PASS(); else FAIL(buf);
    }

    TEST("leading/trailing spaces stripped") {
        NameGen::sanitize(buf, "  hello  ", sizeof(buf));
        if (strcmp(buf, "hello") == 0) PASS(); else FAIL(buf);
    }

    TEST("underscore preserved") {
        NameGen::sanitize(buf, "my_app", sizeof(buf));
        if (strcmp(buf, "my_app") == 0) PASS(); else FAIL(buf);
    }

    TEST("dash preserved") {
        NameGen::sanitize(buf, "my-app", sizeof(buf));
        if (strcmp(buf, "my-app") == 0) PASS(); else FAIL(buf);
    }

    // === Cyrillic ===
    printf("\nCyrillic:\n");

    TEST("привет → privet") {
        NameGen::sanitize(buf, "привет", sizeof(buf));
        if (strcmp(buf, "privet") == 0) PASS(); else FAIL(buf);
    }

    TEST("Москва → moskva") {
        NameGen::sanitize(buf, "Москва", sizeof(buf));
        if (strcmp(buf, "moskva") == 0) PASS(); else FAIL(buf);
    }

    TEST("щука → schuka") {
        NameGen::sanitize(buf, "щука", sizeof(buf));
        if (strcmp(buf, "schuka") == 0) PASS(); else FAIL(buf);
    }

    TEST("жёлтый → zhyoltyj") {
        NameGen::sanitize(buf, "жёлтый", sizeof(buf));
        if (strcmp(buf, "zhyoltyj") == 0) PASS(); else FAIL(buf);
    }

    TEST("Мой Проект → moj-proekt") {
        NameGen::sanitize(buf, "Мой Проект", sizeof(buf));
        if (strcmp(buf, "moj-proekt") == 0) PASS(); else FAIL(buf);
    }

    // === European diacritics ===
    printf("\nDiacritics:\n");

    TEST("über → uber (German)") {
        NameGen::sanitize(buf, "über", sizeof(buf));
        if (strcmp(buf, "uber") == 0) PASS(); else FAIL(buf);
    }

    TEST("café → cafe (French)") {
        NameGen::sanitize(buf, "café", sizeof(buf));
        if (strcmp(buf, "cafe") == 0) PASS(); else FAIL(buf);
    }

    TEST("señor → senor (Spanish)") {
        NameGen::sanitize(buf, "señor", sizeof(buf));
        if (strcmp(buf, "senor") == 0) PASS(); else FAIL(buf);
    }

    // === Edge cases ===
    printf("\nEdge cases:\n");

    TEST("empty string → generated name") {
        NameGen::sanitize(buf, "", sizeof(buf));
        if (strlen(buf) > 0) PASS(); else FAIL("empty output");
    }

    TEST("all symbols → generated name") {
        NameGen::sanitize(buf, "!@#$%", sizeof(buf));
        if (strlen(buf) > 0) PASS(); else FAIL("empty output");
    }

    TEST("mixed Cyrillic + ASCII") {
        NameGen::sanitize(buf, "App Тест 123", sizeof(buf));
        if (strcmp(buf, "app-test-123") == 0) PASS(); else FAIL(buf);
    }

    // === generate ===
    printf("\ngenerate:\n");

    TEST("produces non-empty name") {
        NameGen::generate(buf, sizeof(buf));
        if (strlen(buf) >= 4) PASS(); else FAIL(buf);
    }

    TEST("only lowercase + vowels/consonants") {
        NameGen::generate(buf, sizeof(buf));
        bool ok = true;
        for (int i = 0; buf[i]; i++) {
            if (buf[i] < 'a' || buf[i] > 'z') { ok = false; break; }
        }
        if (ok) PASS(); else FAIL(buf);
    }

    TEST("small buffer handled") {
        char small[4];
        NameGen::generate(small, sizeof(small));
        if (strlen(small) > 0 && strlen(small) < 4) PASS();
        else { FAIL(small); printf("      len=%zu\n", strlen(small)); }
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d NAME GEN TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d NAME GEN TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
