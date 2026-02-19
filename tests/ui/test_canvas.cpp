/**
 * Test: Canvas Drawing
 * Pixel buffer operations: clear, rect, pixel, circle, line
 */
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "ui/ui_html_internal.h"
#include "core/state_store.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static const char* CANVAS_HTML = R"(
<app>
  <ui default="/main">
    <page id="main">
      <canvas id="c1" x="0" y="0" w="20" h="20" bgcolor="#000000"/>
    </page>
  </ui>
</app>
)";

// Read pixel from canvas buffer (returns native color)
static uint32_t getPixel(UI::Element* elem, int x, int y) {
    if (!elem || !elem->canvasBuffer) return 0;
    uint32_t* buf = (uint32_t*)elem->canvasBuffer;
    return buf[y * elem->canvasWidth + x];
}

// Find canvas element by id
static UI::Element* findCanvas(const char* id) {
    for (auto& elem : elements) {
        if (elem->is_canvas && elem->id == id) {
            return elem.get();
        }
    }
    return nullptr;
}

int main() {
    printf("=== Canvas Tests ===\n\n");
    int passed = 0, total = 0;

    LvglMock::create_screen(240, 240);
    State::store().clear();

    auto& ui = UI::Engine::instance();
    ui.init();
    ui.render(CANVAS_HTML);

    auto* canvas = findCanvas("c1");

    // === Creation ===
    printf("Creation:\n");

    TEST("canvas element found") {
        if (canvas) PASS(); else FAIL("null");
    }

    TEST("canvas has buffer") {
        if (canvas && canvas->canvasBuffer) PASS(); else FAIL("no buffer");
    }

    TEST("canvas dimensions 20x20") {
        if (canvas && canvas->canvasWidth == 20 && canvas->canvasHeight == 20) PASS();
        else { FAIL(""); if (canvas) printf("      %dx%d\n", canvas->canvasWidth, canvas->canvasHeight); }
    }

    TEST("canvas is_canvas flag set") {
        if (canvas && canvas->is_canvas) PASS(); else FAIL("");
    }

    if (!canvas || !canvas->canvasBuffer) {
        printf("\nCanvas not available, skipping drawing tests\n");
        printf("\n=== %d/%d CANVAS TESTS PASSED ===\n", passed, total);
        return 1;
    }

    // === Clear ===
    printf("\nClear:\n");

    TEST("canvasClear fills all pixels") {
        // Clear to red (RGB 0xFF0000)
        ui.canvasClear("c1", 0xFF0000);
        // Check corners
        uint32_t p00 = getPixel(canvas, 0, 0);
        uint32_t p99 = getPixel(canvas, 19, 19);
        uint32_t p09 = getPixel(canvas, 0, 19);
        // All should be same non-zero color
        if (p00 != 0 && p00 == p99 && p00 == p09) PASS();
        else { FAIL(""); printf("      p00=0x%08X p99=0x%08X p09=0x%08X\n", p00, p99, p09); }
    }

    TEST("canvasClear different color changes pixels") {
        ui.canvasClear("c1", 0xFF0000); // red
        uint32_t red = getPixel(canvas, 0, 0);
        ui.canvasClear("c1", 0x00FF00); // green
        uint32_t green = getPixel(canvas, 0, 0);
        if (red != green) PASS(); else FAIL("same color");
    }

    // === Pixel ===
    printf("\nPixel:\n");

    TEST("canvasPixel sets single pixel") {
        ui.canvasClear("c1", 0x000000); // black
        uint32_t before = getPixel(canvas, 5, 5);
        ui.canvasPixel("c1", 5, 5, 0xFF0000); // red dot
        uint32_t after = getPixel(canvas, 5, 5);
        // Neighbors unchanged
        uint32_t neighbor = getPixel(canvas, 5, 6);
        if (after != before && neighbor == before) PASS();
        else { FAIL(""); printf("      before=0x%X after=0x%X neighbor=0x%X\n", before, after, neighbor); }
    }

    TEST("canvasPixel out of bounds — no crash") {
        bool ok = ui.canvasPixel("c1", -1, -1, 0xFFFFFF);
        ok = ok && ui.canvasPixel("c1", 100, 100, 0xFFFFFF);
        if (ok) PASS(); else FAIL("");
    }

    // === Rect ===
    printf("\nRect:\n");

    TEST("canvasRect fills area") {
        ui.canvasClear("c1", 0x000000);
        ui.canvasRect("c1", 2, 2, 5, 5, 0xFF0000);
        // Inside rect
        uint32_t inside = getPixel(canvas, 3, 3);
        // Outside rect
        uint32_t outside = getPixel(canvas, 0, 0);
        if (inside != outside && inside != 0) PASS();
        else { FAIL(""); printf("      in=0x%X out=0x%X\n", inside, outside); }
    }

    TEST("canvasRect respects bounds") {
        ui.canvasClear("c1", 0x000000);
        uint32_t bg = getPixel(canvas, 0, 0);
        ui.canvasRect("c1", 5, 5, 3, 3, 0x00FF00);
        // (4,4) should still be background
        uint32_t edge = getPixel(canvas, 4, 4);
        // (5,5) should be green
        uint32_t inside = getPixel(canvas, 5, 5);
        if (edge == bg && inside != bg) PASS();
        else { FAIL(""); printf("      edge=0x%X inside=0x%X bg=0x%X\n", edge, inside, bg); }
    }

    TEST("canvasRect clamps to canvas size") {
        // Rect extends beyond canvas — should not crash
        bool ok = ui.canvasRect("c1", 15, 15, 100, 100, 0xFFFFFF);
        if (ok) PASS(); else FAIL("");
    }

    TEST("canvasRect negative coords clamped") {
        bool ok = ui.canvasRect("c1", -5, -5, 10, 10, 0xFFFFFF);
        if (ok) PASS(); else FAIL("");
    }

    // === Circle ===
    printf("\nCircle:\n");

    TEST("canvasCircle draws filled circle") {
        ui.canvasClear("c1", 0x000000);
        uint32_t bg = getPixel(canvas, 0, 0);
        ui.canvasCircle("c1", 10, 10, 3, 0xFF0000);
        // Center should be colored
        uint32_t center = getPixel(canvas, 10, 10);
        // Far corner should be background
        uint32_t corner = getPixel(canvas, 0, 0);
        if (center != bg && corner == bg) PASS();
        else { FAIL(""); printf("      center=0x%X corner=0x%X bg=0x%X\n", center, corner, bg); }
    }

    TEST("canvasCircle at edge — no crash") {
        bool ok = ui.canvasCircle("c1", 0, 0, 5, 0xFF0000);
        if (ok) PASS(); else FAIL("");
    }

    // === Line ===
    printf("\nLine:\n");

    TEST("canvasLine draws pixels along path") {
        ui.canvasClear("c1", 0x000000);
        uint32_t bg = getPixel(canvas, 0, 0);
        ui.canvasLine("c1", 0, 0, 19, 19, 0xFF0000, 1);
        // Diagonal should have colored pixels
        uint32_t diag = getPixel(canvas, 10, 10);
        // Off-diagonal should be background
        uint32_t off = getPixel(canvas, 0, 19);
        if (diag != bg && off == bg) PASS();
        else { FAIL(""); printf("      diag=0x%X off=0x%X bg=0x%X\n", diag, off, bg); }
    }

    TEST("canvasLine horizontal") {
        ui.canvasClear("c1", 0x000000);
        uint32_t bg = getPixel(canvas, 0, 0);
        ui.canvasLine("c1", 0, 5, 19, 5, 0x00FF00, 1);
        uint32_t on = getPixel(canvas, 10, 5);
        uint32_t off = getPixel(canvas, 10, 0);
        if (on != bg && off == bg) PASS();
        else { FAIL(""); printf("      on=0x%X off=0x%X bg=0x%X\n", on, off, bg); }
    }

    // === Refresh ===
    printf("\nRefresh:\n");

    TEST("canvasRefresh returns true") {
        bool ok = ui.canvasRefresh("c1");
        if (ok) PASS(); else FAIL("");
    }

    // === Error handling ===
    printf("\nErrors:\n");

    TEST("nonexistent canvas returns false") {
        bool ok = ui.canvasClear("nonexistent", 0);
        if (!ok) PASS(); else FAIL("should return false");
    }

    TEST("canvasRect on nonexistent — false") {
        bool ok = ui.canvasRect("bad", 0, 0, 5, 5, 0);
        if (!ok) PASS(); else FAIL("should return false");
    }

    TEST("canvasPixel on nonexistent — false") {
        bool ok = ui.canvasPixel("bad", 0, 0, 0);
        if (!ok) PASS(); else FAIL("should return false");
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CANVAS TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d CANVAS TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
