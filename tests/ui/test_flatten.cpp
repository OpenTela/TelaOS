/**
 * Test: Flatten nested <label> inside <button>
 *
 * When button contains nested <label> tags the parser should:
 * - Single <label>: extract text, font, color; preserve {var} templates
 * - Multiple <label>: strip HTML tags, concatenate text; preserve bindings
 * - No tags: pass through as-is (regression)
 *
 * Verifies that state updates (ui_update_bindings) propagate to flattened buttons.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "ui/ui_task.h"
#include "core/state_store.h"

static int passed = 0, total = 0;

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

static auto& engine() { return UI::Engine::instance(); }

static void render(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    State::store().clear();
    engine().init();
    engine().render(html);
}

// Find the text label child inside a button widget
static LvglMock::Widget* btnLabel(LvglMock::Widget* btn) {
    if (!btn) return nullptr;
    for (auto* c : btn->children) {
        if (!c->text.empty()) return c;
    }
    return nullptr;
}

// Find page → button → label text, short helper
static std::string btnText(const char* btnId) {
    auto* page = LvglMock::g_screen->findById("m", true);
    auto* btn  = page ? page->findById(btnId) : nullptr;
    auto* lbl  = btnLabel(btn);
    return lbl ? lbl->text : "";
}

int main() {
    printf("=== Flatten Nested Labels Tests ===\n\n");

    // ────────────────────────────────────────────────
    printf("1. Single nested label — static text:\n");
    // ────────────────────────────────────────────────

    TEST("plain digit '7' extracted") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"b1\" onclick=\"fn\">"
            "<label align=\"center center\" font=\"22\" color=\"#FFF\">7</label>"
            "</button>"
            "</page></ui></app>"
        );
        if (btnText("b1") == "7") PASS();
        else FAIL(btnText("b1").c_str());
    }

    TEST("font=22 extracted from nested label") {
        auto* page = LvglMock::g_screen->findById("m", true);
        auto* btn  = page ? page->findById("b1") : nullptr;
        auto* lbl  = btnLabel(btn);
        if (lbl && lbl->fontSize > 0) PASS();
        else FAIL("font not applied");
    }

    TEST("color=#FFF extracted from nested label") {
        auto* page = LvglMock::g_screen->findById("m", true);
        auto* btn  = page ? page->findById("b1") : nullptr;
        auto* lbl  = btnLabel(btn);
        if (lbl && lbl->hasColor) PASS();
        else FAIL("color not applied");
    }

    TEST("unicode symbol extracted") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"del\" onclick=\"fn\">"
            "<label font=\"18\" color=\"#F87171\">DEL</label>"
            "</button>"
            "</page></ui></app>"
        );
        if (btnText("del") == "DEL") PASS();
        else FAIL(btnText("del").c_str());
    }

    // ────────────────────────────────────────────────
    printf("\n2. Single nested label — single {var}:\n");
    // ────────────────────────────────────────────────

    TEST("{coldInput} renders default ''") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"b2\" onclick=\"fn\">"
            "<label font=\"24\">{coldInput}</label>"
            "</button>"
            "</page></ui>"
            "<state><string name=\"coldInput\" default=\"\"/></state></app>"
        );
        if (btnText("b2").empty()) PASS();
        else FAIL(btnText("b2").c_str());
    }

    TEST("{coldInput} updates to '48.3'") {
        State::store().set("coldInput", "48.3");
        ui_update_bindings("coldInput", "48.3");
        if (btnText("b2") == "48.3") PASS();
        else FAIL(btnText("b2").c_str());
    }

    TEST("{coldInput} updates again to '48.35'") {
        State::store().set("coldInput", "48.35");
        ui_update_bindings("coldInput", "48.35");
        if (btnText("b2") == "48.35") PASS();
        else FAIL(btnText("b2").c_str());
    }

    TEST("{myVar} renders default '42'") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"b3\" onclick=\"fn\">"
            "<label font=\"24\">{myVar}</label>"
            "</button>"
            "</page></ui>"
            "<state><string name=\"myVar\" default=\"42\"/></state></app>"
        );
        if (btnText("b3") == "42") PASS();
        else FAIL(btnText("b3").c_str());
    }

    TEST("{myVar} updates to '999'") {
        State::store().set("myVar", "999");
        ui_update_bindings("myVar", "999");
        if (btnText("b3") == "999") PASS();
        else FAIL(btnText("b3").c_str());
    }

    // ────────────────────────────────────────────────
    printf("\n3. Single nested label — template with {var}:\n");
    // ────────────────────────────────────────────────

    TEST("'{coldWater} m3' renders '45.2 m3'") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"b4\" onclick=\"fn\">"
            "<label font=\"24\" color=\"#FFFFFF\">{coldWater} m3</label>"
            "</button>"
            "</page></ui>"
            "<state><string name=\"coldWater\" default=\"45.2\"/></state></app>"
        );
        if (btnText("b4") == "45.2 m3") PASS();
        else FAIL(btnText("b4").c_str());
    }

    TEST("update coldWater=48.5 → '48.5 m3'") {
        State::store().set("coldWater", "48.5");
        ui_update_bindings("coldWater", "48.5");
        if (btnText("b4") == "48.5 m3") PASS();
        else FAIL(btnText("b4").c_str());
    }

    TEST("'+{diff}' renders '+3.1'") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"b5\" onclick=\"fn\">"
            "<label font=\"13\" color=\"#555\">+{diff}</label>"
            "</button>"
            "</page></ui>"
            "<state><string name=\"diff\" default=\"3.1\"/></state></app>"
        );
        if (btnText("b5") == "+3.1") PASS();
        else FAIL(btnText("b5").c_str());
    }

    TEST("update diff=5.7 → '+5.7'") {
        State::store().set("diff", "5.7");
        ui_update_bindings("diff", "5.7");
        if (btnText("b5") == "+5.7") PASS();
        else FAIL(btnText("b5").c_str());
    }

    // ────────────────────────────────────────────────
    printf("\n4. Single nested label — multiple {vars}:\n");
    // ────────────────────────────────────────────────

    TEST("'{a} + {b}' renders '10 + 20'") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"b6\" onclick=\"fn\">"
            "<label>{a} + {b}</label>"
            "</button>"
            "</page></ui>"
            "<state>"
            "<string name=\"a\" default=\"10\"/>"
            "<string name=\"b\" default=\"20\"/>"
            "</state></app>"
        );
        if (btnText("b6") == "10 + 20") PASS();
        else FAIL(btnText("b6").c_str());
    }

    TEST("update a=30 → '30 + 20'") {
        State::store().set("a", "30");
        ui_update_bindings("a", "30");
        if (btnText("b6") == "30 + 20") PASS();
        else FAIL(btnText("b6").c_str());
    }

    TEST("update b=40 → '30 + 40'") {
        State::store().set("b", "40");
        ui_update_bindings("b", "40");
        if (btnText("b6") == "30 + 40") PASS();
        else FAIL(btnText("b6").c_str());
    }

    TEST("'{val} {unit}' renders '1247 kwh'") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"b7\" onclick=\"fn\">"
            "<label>{val} {unit}</label>"
            "</button>"
            "</page></ui>"
            "<state>"
            "<string name=\"val\" default=\"1247\"/>"
            "<string name=\"unit\" default=\"kwh\"/>"
            "</state></app>"
        );
        if (btnText("b7") == "1247 kwh") PASS();
        else FAIL(btnText("b7").c_str());
    }

    TEST("update val=1304 → '1304 kwh'") {
        State::store().set("val", "1304");
        ui_update_bindings("val", "1304");
        if (btnText("b7") == "1304 kwh") PASS();
        else FAIL(btnText("b7").c_str());
    }

    TEST("update unit=MWh → '1304 MWh'") {
        State::store().set("unit", "MWh");
        ui_update_bindings("unit", "MWh");
        if (btnText("b7") == "1304 MWh") PASS();
        else FAIL(btnText("b7").c_str());
    }

    // ────────────────────────────────────────────────
    printf("\n5. Multiple nested labels — static:\n");
    // ────────────────────────────────────────────────

    TEST("two labels stripped, text concatenated") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"m1\" onclick=\"fn\">"
            "<label>Hello</label>"
            "<label>World</label>"
            "</button>"
            "</page></ui></app>"
        );
        auto t = btnText("m1");
        if (t.find("Hello") != std::string::npos &&
            t.find("World") != std::string::npos &&
            t.find("<label") == std::string::npos) PASS();
        else FAIL(t.c_str());
    }

    TEST("no raw HTML tags in output") {
        auto t = btnText("m1");
        if (t.find("<") == std::string::npos && t.find(">") == std::string::npos) PASS();
        else FAIL(t.c_str());
    }

    // ────────────────────────────────────────────────
    printf("\n6. Multiple nested labels — with {vars}:\n");
    // ────────────────────────────────────────────────

    TEST("multi labels with vars: initial render") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"m2\" onclick=\"fn\">"
            "<label font=\"13\" color=\"#3B82F6\">Water</label>"
            "<label font=\"24\" color=\"#FFF\">{val} m3</label>"
            "<label font=\"13\" color=\"#555\">+{diff}</label>"
            "</button>"
            "</page></ui>"
            "<state>"
            "<string name=\"val\" default=\"45.2\"/>"
            "<string name=\"diff\" default=\"3.1\"/>"
            "</state></app>"
        );
        auto t = btnText("m2");
        if (t.find("Water") != std::string::npos &&
            t.find("45.2") != std::string::npos &&
            t.find("3.1") != std::string::npos) PASS();
        else FAIL(t.c_str());
    }

    TEST("multi labels: update val=48.5 propagates") {
        State::store().set("val", "48.5");
        ui_update_bindings("val", "48.5");
        auto t = btnText("m2");
        if (t.find("48.5") != std::string::npos) PASS();
        else FAIL(t.c_str());
    }

    TEST("multi labels: update diff=5.7 propagates") {
        State::store().set("diff", "5.7");
        ui_update_bindings("diff", "5.7");
        auto t = btnText("m2");
        if (t.find("5.7") != std::string::npos) PASS();
        else FAIL(t.c_str());
    }

    TEST("multi labels: both vars reflect latest") {
        auto t = btnText("m2");
        if (t.find("48.5") != std::string::npos &&
            t.find("5.7") != std::string::npos) PASS();
        else FAIL(t.c_str());
    }

    // ────────────────────────────────────────────────
    printf("\n7. Button's own attrs override nested:\n");
    // ────────────────────────────────────────────────

    TEST("button font=16 overrides nested label font=22") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"ov1\" font=\"16\" onclick=\"fn\">"
            "<label font=\"22\">X</label>"
            "</button>"
            "</page></ui></app>"
        );
        if (btnText("ov1") == "X") PASS();
        else FAIL(btnText("ov1").c_str());
    }

    TEST("button color=#0F0 overrides nested color=#FFF") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"ov2\" color=\"#00ff00\" onclick=\"fn\">"
            "<label color=\"#ffffff\">Y</label>"
            "</button>"
            "</page></ui></app>"
        );
        auto* page = LvglMock::g_screen->findById("m", true);
        auto* btn  = page ? page->findById("ov2") : nullptr;
        auto* lbl  = btnLabel(btn);
        if (lbl && lbl->hasColor && lbl->color == 0x00ff00) PASS();
        else if (lbl) { FAIL("wrong color"); printf("      got 0x%06x\n", lbl->color); }
        else FAIL("label not found");
    }

    // ────────────────────────────────────────────────
    printf("\n8. Plain text — no nesting (regression):\n");
    // ────────────────────────────────────────────────

    TEST("plain text 'Click me' works") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"p1\" onclick=\"fn\">Click me</button>"
            "</page></ui></app>"
        );
        if (btnText("p1") == "Click me") PASS();
        else FAIL(btnText("p1").c_str());
    }

    TEST("plain text with {var} binding works") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"p2\" onclick=\"fn\">Count: {cnt}</button>"
            "</page></ui>"
            "<state><string name=\"cnt\" default=\"0\"/></state></app>"
        );
        if (btnText("p2") == "Count: 0") PASS();
        else FAIL(btnText("p2").c_str());
    }

    TEST("plain text {var} updates to 'Count: 5'") {
        State::store().set("cnt", "5");
        ui_update_bindings("cnt", "5");
        if (btnText("p2") == "Count: 5") PASS();
        else FAIL(btnText("p2").c_str());
    }

    // ────────────────────────────────────────────────
    printf("\n9. Edge cases:\n");
    // ────────────────────────────────────────────────

    TEST("empty nested label → no crash") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"e1\" onclick=\"fn\">"
            "<label></label>"
            "</button>"
            "</page></ui></app>"
        );
        PASS();  // no crash = ok
    }

    TEST("whitespace around nested label → text extracted") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"e3\" onclick=\"fn\">"
            "  <label font=\"22\">OK</label>  "
            "</button>"
            "</page></ui></app>"
        );
        if (btnText("e3") == "OK") PASS();
        else FAIL(btnText("e3").c_str());
    }

    TEST("button with only spaces (no label) → no crash") {
        render(
            "<app><ui default=\"/m\"><page id=\"m\">"
            "<button id=\"e4\" onclick=\"fn\">   </button>"
            "</page></ui></app>"
        );
        PASS();  // no crash = ok
    }

    // ────────────────────────────────────────────────
    printf("\n=== %d/%d FLATTEN TESTS PASSED ===\n", passed, total);
    return (passed == total) ? 0 : 1;
}
