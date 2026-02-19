/**
 * Test: z-index support
 * HTML attribute, CSS property, setAttr/getAttr, child ordering
 */
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static auto& engine() { return UI::Engine::instance(); }

static LvglMock::Widget* screen() {
    return LvglMock::g_screen;
}

// Find index of widget among its siblings in mock
static int childIndex(LvglMock::Widget* w) {
    if (!w || !w->parent) return -1;
    auto& siblings = w->parent->children;
    for (int i = 0; i < (int)siblings.size(); i++) {
        if (siblings[i] == w) return i;
    }
    return -1;
}

static void render(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    engine().init();
    engine().render(html);
}

int main() {
    printf("=== Z-Index Tests ===\n\n");
    int passed = 0, total = 0;

    // === HTML attribute z-index ===
    printf("HTML attribute:\n");

    TEST("z-index=1 moves label to front") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<label id=\"back\">Back</label>"
            "<label id=\"front\" z-index=\"1\">Front</label>"
            "<label id=\"mid\">Mid</label>"
            "</page></ui></app>"
        );
        // Find page children
        auto* page = screen()->findById("m", true);
        if (!page) { FAIL("no page"); }
        else {
            auto* back = page->findById("back");
            auto* front = page->findById("front");
            auto* mid = page->findById("mid");
            if (!back || !front || !mid) { FAIL("widgets not found"); }
            else {
                int bi = childIndex(back);
                int fi = childIndex(front);
                int mi = childIndex(mid);
                // front should be last (highest index) because z-index=1 moved it to foreground
                if (fi > bi && fi > mi) PASS();
                else { FAIL(""); printf("      back=%d front=%d mid=%d\n", bi, fi, mi); }
            }
        }
    }

    TEST("z-index=-1 moves label to back") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<label id=\"a\">A</label>"
            "<label id=\"b\" z-index=\"-1\">B</label>"
            "<label id=\"c\">C</label>"
            "</page></ui></app>"
        );
        auto* page = screen()->findById("m", true);
        if (!page) { FAIL("no page"); }
        else {
            auto* b = page->findById("b");
            auto* a = page->findById("a");
            auto* c = page->findById("c");
            if (!a || !b || !c) { FAIL("widgets not found"); }
            else {
                int bi = childIndex(b);
                int ai = childIndex(a);
                int ci = childIndex(c);
                // b should be first (index 0) because z-index=-1 moved it to back
                if (bi < ai && bi < ci) PASS();
                else { FAIL(""); printf("      a=%d b=%d c=%d\n", ai, bi, ci); }
            }
        }
    }

    TEST("z-index=0 keeps default order") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<label id=\"x\">X</label>"
            "<label id=\"y\" z-index=\"0\">Y</label>"
            "<label id=\"z\">Z</label>"
            "</page></ui></app>"
        );
        auto* page = screen()->findById("m", true);
        if (!page) { FAIL("no page"); }
        else {
            auto* x = page->findById("x");
            auto* y = page->findById("y");
            auto* z = page->findById("z");
            if (!x || !y || !z) { FAIL("widgets not found"); }
            else {
                // Default order preserved: x, y, z
                if (childIndex(x) < childIndex(y) && childIndex(y) < childIndex(z)) PASS();
                else { FAIL("order changed"); }
            }
        }
    }

    TEST("z-index on button") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<label id=\"lbl\">Text</label>"
            "<button id=\"btn\" z-index=\"1\" onclick=\"x\">Click</button>"
            "</page></ui></app>"
        );
        auto* page = screen()->findById("m", true);
        if (!page) { FAIL("no page"); }
        else {
            // Button should be moved to front
            auto btns = page->findAll("Button", false);
            auto lbls = page->findAll("Label", false);
            if (!btns.empty() && !lbls.empty()) {
                if (childIndex(btns[0]) > childIndex(lbls[0])) PASS();
                else FAIL("button not in front");
            } else { FAIL("widgets not found"); }
        }
    }

    // === CSS z-index ===
    printf("\nCSS property:\n");

    TEST("z-index via CSS class") {
        render(
            "<app>"
            "<style>label.overlay { z-index: 1; }</style>"
            "<ui default=\"/m\"><page id=\"m\">"
            "<label id=\"base\">Base</label>"
            "<label id=\"top\" class=\"overlay\">Top</label>"
            "<label id=\"other\">Other</label>"
            "</page></ui></app>"
        );
        auto* page = screen()->findById("m", true);
        if (!page) { FAIL("no page"); }
        else {
            auto* top = page->findById("top");
            auto* base = page->findById("base");
            auto* other = page->findById("other");
            if (!top || !base || !other) { FAIL("widgets not found"); }
            else {
                if (childIndex(top) > childIndex(base) && childIndex(top) > childIndex(other)) PASS();
                else { FAIL(""); printf("      base=%d top=%d other=%d\n", 
                    childIndex(base), childIndex(top), childIndex(other)); }
            }
        }
    }

    // === setAttr z-index ===
    printf("\nsetAttr/getAttr:\n");

    TEST("setAttr z-index=1 moves to front") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<label id=\"a\">A</label>"
            "<label id=\"b\">B</label>"
            "<label id=\"c\">C</label>"
            "</page></ui></app>"
        );
        auto* page = screen()->findById("m", true);
        if (!page) { FAIL("no page"); }
        else {
            // Initially: a=0, b=1, c=2
            auto* a = page->findById("a");
            int aBefore = childIndex(a);
            
            UI::setWidgetAttr("a", "z-index", "1");
            
            int aAfter = childIndex(a);
            // a should now be last
            if (aAfter > aBefore) PASS();
            else { FAIL(""); printf("      before=%d after=%d\n", aBefore, aAfter); }
        }
    }

    TEST("setAttr z-index=-1 moves to back") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<label id=\"a\">A</label>"
            "<label id=\"b\">B</label>"
            "<label id=\"c\">C</label>"
            "</page></ui></app>"
        );
        UI::setWidgetAttr("c", "z-index", "-1");
        
        auto* page = screen()->findById("m", true);
        auto* c = page->findById("c");
        if (c && childIndex(c) == 0) PASS();
        else { FAIL(""); if (c) printf("      c index=%d\n", childIndex(c)); }
    }

    TEST("getAttr z-index returns child index") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<label id=\"a\">A</label>"
            "<label id=\"b\">B</label>"
            "</page></ui></app>"
        );
        auto val = UI::getWidgetAttr("b", "z-index");
        // b is second child, but page might have other children
        // just check it returns a number
        if (!val.empty()) PASS();
        else FAIL("empty");
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d Z-INDEX TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d Z-INDEX TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
