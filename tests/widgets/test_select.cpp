/**
 * Test: <select> / <option> widget
 *
 * Tests:
 * - Dropdown создаётся из <select> + <option>
 * - Начальный индекс выставляется из state (bind)
 * - Обратный биндинг: ui_update_bindings → curValue
 * - <option> без value= использует текст как value
 * - Шаблоны {var} в тексте option раскрываются
 * - CSS-стили применяются
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "ui/ui_task.h"
#include "core/state_store.h"

static int passed = 0, total = 0;

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

static Core& engine() { return g_core; }

static void render(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(html);
}

int main() {
    printf("=== Select Widget Tests ===\n\n");

    // ── Test 1: Dropdown создаётся ─────────────────────────────────
    printf("Parsing:\n");
    {
        render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <select id="cityDd" x="5%" y="20%" w="90%" h="40" bind="city">
        <option value="moscow">Москва</option>
        <option value="london">London</option>
        <option value="tokyo">Tokyo</option>
      </select>
    </page>
  </ui>
  <state><string name="city" default="moscow"/></state>
</app>)");

        auto* page = LvglMock::g_screen->first("Container");

        TEST("Dropdown widget создан") {
            auto* w = page ? page->findById("cityDd") : nullptr;
            if (w && w->type == "Dropdown") PASS();
            else FAIL(w ? w->type.c_str() : "not found");
        }

        TEST("Options string содержит все 3 пункта") {
            auto* w = page ? page->findById("cityDd") : nullptr;
            if (w &&
                w->text.find("Москва") != std::string::npos &&
                w->text.find("London") != std::string::npos &&
                w->text.find("Tokyo")  != std::string::npos) PASS();
            else FAIL(w ? w->text.c_str() : "not found");
        }

        TEST("Начальный индекс = 0 (city=moscow → index 0)") {
            auto* w = page ? page->findById("cityDd") : nullptr;
            if (w && w->curValue == 0) PASS();
            else { FAIL(""); if(w) printf("      got %d\n", w->curValue); }
        }
    }

    // ── Test 2: Начальное состояние = не первый элемент ────────────
    printf("\nInitial binding:\n");
    {
        LvglMock::reset();
        LvglMock::create_screen(480, 480);
        g_core.store().clear();
        g_core.store().defineString("city", "london");
        g_core.initDynamicApp(nullptr);
        g_core.render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <select id="cityDd" bind="city">
        <option value="moscow">Москва</option>
        <option value="london">London</option>
        <option value="tokyo">Tokyo</option>
      </select>
    </page>
  </ui>
  <state><string name="city" default="london"/></state>
</app>)");

        auto* page = LvglMock::g_screen->first("Container");

        TEST("city=london → начальный индекс = 1") {
            auto* w = page ? page->findById("cityDd") : nullptr;
            if (w && w->curValue == 1) PASS();
            else { FAIL(""); if(w) printf("      got %d\n", w->curValue); }
        }
    }

    // ── Test 3: Обратный биндинг state → dropdown ──────────────────
    printf("\nReverse binding (state → dropdown):\n");
    {
        render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <select id="cityDd" bind="city">
        <option value="moscow">Москва</option>
        <option value="london">London</option>
        <option value="tokyo">Tokyo</option>
      </select>
    </page>
  </ui>
  <state><string name="city" default="moscow"/></state>
</app>)");

        auto* page = LvglMock::g_screen->first("Container");

        g_core.store().set("city", "tokyo");
        ui_update_bindings("city", "tokyo");

        TEST("ui_update_bindings(city=tokyo) → index 2") {
            auto* w = page ? page->findById("cityDd") : nullptr;
            if (w && w->curValue == 2) PASS();
            else { FAIL(""); if(w) printf("      got %d\n", w->curValue); }
        }

        g_core.store().set("city", "moscow");
        ui_update_bindings("city", "moscow");

        TEST("ui_update_bindings(city=moscow) → index 0") {
            auto* w = page ? page->findById("cityDd") : nullptr;
            if (w && w->curValue == 0) PASS();
            else { FAIL(""); if(w) printf("      got %d\n", w->curValue); }
        }
    }

    // ── Test 4: <option> без value= → label как value ──────────────
    printf("\n<option> without value=:\n");
    {
        render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <select id="themeDd" bind="theme">
        <option>Dark</option>
        <option>Light</option>
        <option>Auto</option>
      </select>
    </page>
  </ui>
  <state><string name="theme" default="Light"/></state>
</app>)");

        auto* page = LvglMock::g_screen->first("Container");

        TEST("theme=Light (нет value=) → начальный индекс = 1") {
            auto* w = page ? page->findById("themeDd") : nullptr;
            if (w && w->curValue == 1) PASS();
            else { FAIL(""); if(w) printf("      got %d\n", w->curValue); }
        }

        g_core.store().set("theme", "Auto");
        ui_update_bindings("theme", "Auto");

        TEST("ui_update_bindings(theme=Auto) → index 2") {
            auto* w = page ? page->findById("themeDd") : nullptr;
            if (w && w->curValue == 2) PASS();
            else { FAIL(""); if(w) printf("      got %d\n", w->curValue); }
        }
    }

    // ── Test 5: Шаблоны {var} в тексте option ─────────────────────
    printf("\nTemplate vars in option labels:\n");
    {
        render(R"(
<app os="1.0">
  <ui default="/main">
    <page id="main">
      <select id="dynDd" bind="level">
        <option value="0">{name0}</option>
        <option value="1">{name1}</option>
        <option value="2">{name2}</option>
      </select>
    </page>
  </ui>
  <state>
    <string name="level" default="0"/>
    <string name="name0" default="Alpha"/>
    <string name="name1" default="Beta"/>
    <string name="name2" default="Gamma"/>
  </state>
</app>)");

        auto* page = LvglMock::g_screen->first("Container");

        TEST("Шаблоны раскрыты: options содержит Alpha/Beta/Gamma") {
            auto* w = page ? page->findById("dynDd") : nullptr;
            if (w &&
                w->text.find("Alpha") != std::string::npos &&
                w->text.find("Beta")  != std::string::npos &&
                w->text.find("Gamma") != std::string::npos) PASS();
            else FAIL(w ? w->text.c_str() : "not found");
        }
    }

    // ── Test 6: CSS применяется ────────────────────────────────────
    printf("\nCSS styling:\n");
    {
        render(R"(
<app os="1.0">
  <style>
    select { bgcolor: #333333; color: #ffffff; }
    select.primary { bgcolor: #0066ff; }
  </style>
  <ui default="/main">
    <page id="main">
      <select id="s1" bind="v1">
        <option value="a">A</option>
      </select>
      <select id="s2" class="primary" bind="v2">
        <option value="a">A</option>
      </select>
    </page>
  </ui>
  <state>
    <string name="v1" default="a"/>
    <string name="v2" default="a"/>
  </state>
</app>)");

        auto* page = LvglMock::g_screen->first("Container");

        TEST("select tag rule → bgcolor #333333") {
            auto* w = page ? page->findById("s1") : nullptr;
            if (w && w->hasBgcolor && w->bgcolor == 0x333333) PASS();
            else { FAIL(""); if(w) printf("      got 0x%06x hasBgcolor=%d\n", w->bgcolor, w->hasBgcolor); }
        }

        TEST("select tag rule → color #ffffff") {
            auto* w = page ? page->findById("s1") : nullptr;
            if (w && w->hasColor && w->color == 0xffffff) PASS();
            else { FAIL(""); if(w) printf("      got 0x%06x\n", w->color); }
        }

        TEST("select.primary → bgcolor #0066ff") {
            auto* w = page ? page->findById("s2") : nullptr;
            if (w && w->hasBgcolor && w->bgcolor == 0x0066ff) PASS();
            else { FAIL(""); if(w) printf("      got 0x%06x\n", w->bgcolor); }
        }
    }

    // ── Summary ────────────────────────────────────────────────────
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
