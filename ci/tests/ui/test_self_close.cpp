/**
 * test_self_close.cpp — a self-closing tag stays self-closing on pages with @for / templates.
 *
 * Such a page is parsed from an expanded copy of its markup. The self-closing check once
 * compared the scan position with the start of the ORIGINAL markup — another allocation —
 * so <label .../> worked or swallowed the following markup depending on where the copy
 * landed in memory (crossword · main in the simulator). Here the original markup sits on
 * the stack, above the heap that holds the copy: exactly the layout that broke.
 */

#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "ui/ui_html_internal.h"

#define TEST(name) printf("  %-60s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

// The top of crossword · main: background bar, then two labels, then an @for
static const char* PAGE = R"HTML(
      <label id="bar" x="2%" y="1%" w="96%" h="36" bgcolor="#1c1c1c"/>
      <label id="dir" x="4%" y="2%" w="10%">D</label>
      <label id="hint" x="14%" y="2%" w="84%">H</label>
      <button id="ok"/>
      <label id="after">A</label>
      @for(i in 0..2) {
        <button id="key_{i}">K</button>
      }
)HTML";

static LvglMock::Widget* parsePage(const char* markup) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    char onStack[2048];  // the original markup: on the stack, above the heap copy
    const int len = (int)strlen(markup);
    memcpy(onStack, markup, (size_t)len + 1);
    parse_children(onStack, len, lv_screen_active());
    return LvglMock::g_screen;
}

int main() {
    printf("=== SELF-CLOSING TAGS AFTER @for EXPANSION ===\n\n");
    int passed = 0, total = 0;

    LvglMock::Widget* page = parsePage(PAGE);

    TEST("@for expanded: 3 keys") {
        int keys = 0;
        for (int i = 0; i < 3; i++) {
            char id[16];
            snprintf(id, sizeof(id), "key_%d", i);
            if (page->findById(id)) keys++;
        }
        if (keys == 3) PASS();
        else { char buf[48]; snprintf(buf, sizeof(buf), "expected 3, got %d", keys); FAIL(buf); }
    }

    TEST("<label .../> has no text") {
        LvglMock::Widget* bar = page->findById("bar");
        if (!bar) FAIL("bar not found");
        else if (bar->text.empty()) PASS();
        else { char buf[96]; snprintf(buf, sizeof(buf), "swallowed: \"%.60s\"", bar->text.c_str()); FAIL(buf); }
    }

    TEST("the label after it exists, with its text") {
        LvglMock::Widget* dir = page->findById("dir");
        if (dir && dir->text == "D") PASS();
        else FAIL(dir ? dir->text.c_str() : "dir not found (swallowed by <label .../>)");
    }

    TEST("the next one too") {
        LvglMock::Widget* hint = page->findById("hint");
        if (hint && hint->text == "H") PASS();
        else FAIL(hint ? hint->text.c_str() : "hint not found");
    }

    TEST("<button/> is self-closing as well") {
        LvglMock::Widget* after = page->findById("after");
        if (page->findById("ok") && after && after->text == "A") PASS();
        else FAIL(after ? after->text.c_str() : "label after <button/> not found");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d SELF-CLOSE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d SELF-CLOSE TESTS PASSED ===\n", passed, total);
    return 1;
}
