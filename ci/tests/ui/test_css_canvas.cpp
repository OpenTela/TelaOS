/**
 * Test: html {} and body {} on the page, as a browser puts them on its canvas (docs/CSS.md)
 *
 * The page takes html's background, or body's when html has none or a transparent one, and the
 * color its text inherits: body's, or html's. var()s are put in. A page with its own bgcolor=""
 * keeps it. A <style> on a page counts for that page and the ones after it, as for its content.
 */
#include <cstdio>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-58s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static int passed = 0;
static int total = 0;

// The page objects, in the order the app has them
static LvglMock::Widget* page(int i) {
    int n = 0;
    for (auto* w : LvglMock::g_screen->children) {
        if (n++ == i)
            return w;
    }
    return nullptr;
}

static void render(const char* app) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.render(app);
}

static void expectBg(const char* name, int i, bool has, uint32_t want = 0) {
    TEST(name);
    auto* w = page(i);
    const bool ok = w && w->hasBgcolor == has && (!has || w->bgcolor == want);
    if (ok) {
        PASS();
        return;
    }
    FAIL("");
    if (w)
        printf("      hasBgcolor=%d bgcolor=0x%06x\n", w->hasBgcolor, w->bgcolor);
}

static void expectColor(const char* name, int i, bool has, uint32_t want = 0) {
    TEST(name);
    auto* w = page(i);
    const bool ok = w && w->hasColor == has && (!has || w->color == want);
    if (ok) {
        PASS();
        return;
    }
    FAIL("");
    if (w)
        printf("      hasColor=%d color=0x%06x\n", w->hasColor, w->color);
}

int main() {
    printf("=== html {} and body {} on the page ===\n\n");

    render(R"(<app><style>body { background: #ffffff; color: #101010; }</style>
      <ui default="/a"><page id="a"><p>x</p></page></ui></app>)");
    expectBg("body's background: the page's", 0, true, 0xffffff);
    expectColor("body's color: the color its text inherits", 0, true, 0x101010);
    TEST("HTML on the page: its body box takes the color too");
    {
        auto* p = page(0);
        auto* root = p && !p->children.empty() ? p->children.front() : nullptr;
        if (root && root->hasColor && root->color == 0x101010)
            PASS();
        else
            FAIL("");
    }

    render(R"(<app><style>html { background: #202020; color: #303030; } body { background: #ffffff; }</style>
      <ui default="/a"><page id="a"><p>x</p></page></ui></app>)");
    expectBg("html's background wins over body's", 0, true, 0x202020);
    expectColor("no color in body: html's", 0, true, 0x303030);

    render(R"(<app><style>html { background: transparent; } body { background: #ffffff; }</style>
      <ui default="/a"><page id="a"><p>x</p></page></ui></app>)");
    expectBg("html's transparent: body's", 0, true, 0xffffff);

    render(R"(<app><style>:root { --bg: #123456; --fg: #654321; } html, body { background: var(--bg); color: var(--fg); }</style>
      <ui default="/a"><page id="a"><p>x</p></page></ui></app>)");
    expectBg("var() put in: background", 0, true, 0x123456);
    expectColor("var() put in: color", 0, true, 0x654321);

    render(R"(<app><style>body { background: #ffffff; }</style>
      <ui default="/a"><page id="a" bgcolor="#00ff00"><p>x</p></page></ui></app>)");
    expectBg("bgcolor=\"\" of the page stays", 0, true, 0x00ff00);

    render(R"(<app><style>p { color: #ff0000; }</style>
      <ui default="/a"><page id="a"><p>x</p></page></ui></app>)");
    expectBg("no html, no body: the page as it was", 0, false);
    expectColor("no html, no body: no color of its own", 0, false);

    render(R"(<app><ui default="/a">
      <page id="a"><p>first</p></page>
      <page id="b"><style>body { background: #0000ff; }</style><p>second</p></page>
      <page id="c"><p>third</p></page></ui></app>)");
    expectBg("<style> on a page: not the pages before it", 0, false);
    expectBg("<style> on a page: that page", 1, true, 0x0000ff);
    expectBg("<style> on a page: the pages after it", 2, true, 0x0000ff);

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CANVAS TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d CANVAS TESTS PASSED ===\n", passed, total);
    return 1;
}
