/**
 * test_table.cpp — Tests for <table>/<tr>/<td> layout containers
 *
 * Tests:
 * - Basic 2x2: hierarchy, child positions in grid
 * - Mixed widgets: label + button in cells
 * - Single row: 3 columns
 * - CSS classes on td: width/height from CSS
 */

#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/core.h"
#include "core/state_store.h"

using MockWidget = LvglMock::Widget;

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

static const char* HTML_BASIC = R"HTML(
<app>
  <ui default="/main">
    <page id="main">
      <table x="5%" y="5%" w="90%">
        <tr>
          <td w="50%" h="40"><button id="A1" w="100%" h="100%" bgcolor="#333">A1</button></td>
          <td w="50%" h="40"><button id="B1" w="100%" h="100%" bgcolor="#444">B1</button></td>
        </tr>
        <tr>
          <td w="50%" h="40"><button id="A2" w="100%" h="100%" bgcolor="#444">A2</button></td>
          <td w="50%" h="40"><button id="B2" w="100%" h="100%" bgcolor="#333">B2</button></td>
        </tr>
      </table>
    </page>
  </ui>
  <state/>
  <script language="lua"></script>
</app>
)HTML";

static const char* HTML_MIXED = R"HTML(
<app>
  <ui default="/main">
    <page id="main">
      <table x="0" y="10%" w="100%">
        <tr>
          <td w="30%" h="30"><label id="hdr1" color="#888">Name</label></td>
          <td w="70%" h="30"><label id="hdr2" color="#888">Value</label></td>
        </tr>
        <tr>
          <td w="30%" h="40"><label id="nameLabel">{name}</label></td>
          <td w="70%" h="40"><button id="valBtn" onclick="doTap">{value}</button></td>
        </tr>
      </table>
    </page>
  </ui>
  <state>
    <string name="name" default="Test"/>
    <string name="value" default="42"/>
  </state>
  <script language="lua">
    function doTap() end
  </script>
</app>
)HTML";

static const char* HTML_SINGLE_ROW = R"HTML(
<app>
  <ui default="/main">
    <page id="main">
      <table w="100%">
        <tr>
          <td><button id="b1">1</button></td>
          <td><button id="b2">2</button></td>
          <td><button id="b3">3</button></td>
        </tr>
      </table>
    </page>
  </ui>
  <state/>
  <script language="lua"></script>
</app>
)HTML";

static const char* HTML_CSS = R"HTML(
<app>
  <ui default="/main">
    <page id="main">
      <table w="100%">
        <tr>
          <td class="wide"><label id="l1">Wide</label></td>
          <td class="narrow"><label id="l2">Narrow</label></td>
        </tr>
        <tr>
          <td class="wide"><button id="b1" class="cell">B1</button></td>
          <td class="narrow"><button id="b2" class="cell">B2</button></td>
        </tr>
      </table>
    </page>
  </ui>
  <style>
    .wide { width: 70%; height: 40; }
    .narrow { width: 30%; height: 40; }
    .cell { width: 100%; height: 100%; bgcolor: #333; radius: 0; }
  </style>
  <state/>
  <script language="lua"></script>
</app>
)HTML";

static void render(const char* html) {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    g_core.store().clear();
    g_core.initDynamicApp(nullptr);
    g_core.render(html);
}

static MockWidget* findTable(MockWidget* page) {
    if (!page) return nullptr;
    for (auto* child : page->children) {
        if (child->type == "Container") return child;
    }
    return nullptr;
}

static MockWidget* getTr(MockWidget* table, int index) {
    if (!table) return nullptr;
    int trIdx = 0;
    for (auto* child : table->children) {
        if (child->type == "Container") {
            if (trIdx == index) return child;
            trIdx++;
        }
    }
    return nullptr;
}

static MockWidget* getTd(MockWidget* tr, int index) {
    if (!tr) return nullptr;
    int tdIdx = 0;
    for (auto* child : tr->children) {
        if (child->type == "Container") {
            if (tdIdx == index) return child;
            tdIdx++;
        }
    }
    return nullptr;
}

static MockWidget* getCellWidget(MockWidget* td) {
    if (!td || td->children.empty()) return nullptr;
    return td->children[0];
}

int main() {
    printf("=== TABLE LAYOUT TESTS ===\n\n");
    int passed = 0, total = 0;

    printf("Basic 2x2 table:\n");
    render(HTML_BASIC);
    auto* page = LvglMock::g_screen->first("Container");
    auto* table = findTable(page);

    TEST("table container exists") {
        if (table) PASS();
        else FAIL("no table container found");
    }

    TEST("table has 2 rows (tr)") {
        if (!table) { FAIL("no table"); }
        else {
            int rows = 0;
            for (auto* c : table->children) if (c->type == "Container") rows++;
            if (rows == 2) PASS();
            else { char buf[64]; snprintf(buf, sizeof(buf), "expected 2 rows, got %d", rows); FAIL(buf); }
        }
    }

    TEST("row 0 has 2 cells (td)") {
        auto* tr0 = getTr(table, 0);
        if (!tr0) { FAIL("no row 0"); }
        else {
            int cells = 0;
            for (auto* c : tr0->children) if (c->type == "Container") cells++;
            if (cells == 2) PASS();
            else { char buf[64]; snprintf(buf, sizeof(buf), "expected 2 cells, got %d", cells); FAIL(buf); }
        }
    }

    TEST("row 1 has 2 cells (td)") {
        auto* tr1 = getTr(table, 1);
        if (!tr1) { FAIL("no row 1"); }
        else {
            int cells = 0;
            for (auto* c : tr1->children) if (c->type == "Container") cells++;
            if (cells == 2) PASS();
            else { char buf[64]; snprintf(buf, sizeof(buf), "expected 2 cells, got %d", cells); FAIL(buf); }
        }
    }

    TEST("A1 is in row 0, col 0") {
        auto* w = getCellWidget(getTd(getTr(table, 0), 0));
        if (w && w->findById("A1")) PASS();
        else FAIL("A1 not at [0][0]");
    }

    TEST("B1 is in row 0, col 1") {
        auto* w = getCellWidget(getTd(getTr(table, 0), 1));
        if (w && w->findById("B1")) PASS();
        else FAIL("B1 not at [0][1]");
    }

    TEST("A2 is in row 1, col 0") {
        auto* w = getCellWidget(getTd(getTr(table, 1), 0));
        if (w && w->findById("A2")) PASS();
        else FAIL("A2 not at [1][0]");
    }

    TEST("B2 is in row 1, col 1") {
        auto* w = getCellWidget(getTd(getTr(table, 1), 1));
        if (w && w->findById("B2")) PASS();
        else FAIL("B2 not at [1][1]");
    }

    TEST("4 buttons total") {
        int count = page->count("Button", true);
        if (count == 4) PASS();
        else { char buf[64]; snprintf(buf, sizeof(buf), "expected 4, got %d", count); FAIL(buf); }
    }

    printf("\nMixed widgets in table:\n");
    render(HTML_MIXED);
    page = LvglMock::g_screen->first("Container");
    table = findTable(page);

    TEST("hdr1 (label) in row 0, col 0") {
        auto* td = getTd(getTr(table, 0), 0);
        if (td && td->find("Label")) PASS();
        else FAIL("no label at [0][0]");
    }

    TEST("valBtn (button) in row 1, col 1") {
        auto* td = getTd(getTr(table, 1), 1);
        if (td && td->find("Button")) PASS();
        else FAIL("no button at [1][1]");
    }

    TEST("nameLabel in row 1, col 0") {
        auto* td = getTd(getTr(table, 1), 0);
        if (td && td->findById("nameLabel")) PASS();
        else FAIL("nameLabel not at [1][0]");
    }

    TEST("nameLabel text = 'Test'") {
        auto* w = page->findById("nameLabel");
        if (w && w->text == "Test") PASS();
        else FAIL(w ? w->text.c_str() : "not found");
    }

    printf("\nSingle row (3 columns):\n");
    render(HTML_SINGLE_ROW);
    page = LvglMock::g_screen->first("Container");
    table = findTable(page);

    TEST("1 row in table") {
        if (!table) { FAIL("no table"); }
        else {
            int rows = 0;
            for (auto* c : table->children) if (c->type == "Container") rows++;
            if (rows == 1) PASS();
            else { char buf[64]; snprintf(buf, sizeof(buf), "expected 1 row, got %d", rows); FAIL(buf); }
        }
    }

    TEST("3 cells in row") {
        auto* tr0 = getTr(table, 0);
        if (!tr0) { FAIL("no row"); }
        else {
            int cells = 0;
            for (auto* c : tr0->children) if (c->type == "Container") cells++;
            if (cells == 3) PASS();
            else { char buf[64]; snprintf(buf, sizeof(buf), "expected 3 cells, got %d", cells); FAIL(buf); }
        }
    }

    TEST("b1 in col 0") {
        if (getTd(getTr(table, 0), 0) && getTd(getTr(table, 0), 0)->findById("b1")) PASS();
        else FAIL("b1 not at col 0");
    }

    TEST("b2 in col 1") {
        if (getTd(getTr(table, 0), 1) && getTd(getTr(table, 0), 1)->findById("b2")) PASS();
        else FAIL("b2 not at col 1");
    }

    TEST("b3 in col 2") {
        if (getTd(getTr(table, 0), 2) && getTd(getTr(table, 0), 2)->findById("b3")) PASS();
        else FAIL("b3 not at col 2");
    }

    printf("\nCSS on table/tr/td:\n");
    render(HTML_CSS);
    page = LvglMock::g_screen->first("Container");
    table = findTable(page);

    TEST("table exists with CSS app") {
        if (table) PASS();
        else FAIL("no table");
    }

    TEST("2 rows in CSS table") {
        if (!table) { FAIL("no table"); }
        else {
            int rows = 0;
            for (auto* c : table->children) if (c->type == "Container") rows++;
            if (rows == 2) PASS();
            else { char buf[64]; snprintf(buf, sizeof(buf), "expected 2, got %d", rows); FAIL(buf); }
        }
    }

    TEST("wide td has w from CSS") {
        auto* td = getTd(getTr(table, 0), 0);
        if (td && td->w > 0) PASS();
        else FAIL(td ? "w=0" : "not found");
    }

    TEST("narrow td has w from CSS") {
        auto* td = getTd(getTr(table, 0), 1);
        if (td && td->w > 0) PASS();
        else FAIL(td ? "w=0" : "not found");
    }

    TEST("wide td wider than narrow td") {
        auto* wide = getTd(getTr(table, 0), 0);
        auto* narrow = getTd(getTr(table, 0), 1);
        if (wide && narrow && wide->w > narrow->w) PASS();
        else {
            char buf[64];
            snprintf(buf, sizeof(buf), "wide=%d narrow=%d",
                     wide ? wide->w : -1, narrow ? narrow->w : -1);
            FAIL(buf);
        }
    }

    TEST("button b1 exists inside CSS-styled td") {
        if (page->findById("b1")) PASS();
        else FAIL("not found");
    }

    TEST("label l1 exists inside CSS-styled td") {
        if (page->findById("l1")) PASS();
        else FAIL("not found");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d TABLE TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d TABLE TESTS PASSED ===\n", passed, total);
        return 1;
    }
}
