/**
 * Test: <link rel="stylesheet" href> — a stylesheet in a file (docs/CSS.md, «Где CSS»)
 *
 * In <app> it counts for the whole app, where it stands among the <style>; on a page, for what
 * comes after it. href from / is a file of the watch, another is in the app's resources/.
 * With media, only for a screen that fits it. Left out, as a browser leaves them out: another
 * rel, rel="alternate stylesheet", disabled, an address of the network, a file that is not there.
 */
#include <cstdio>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "LittleFS.h"
#include "core/core.h"
#include "core/state_store.h"

static const char* APP = R"BAX(
<app>
  <link rel="stylesheet" href="first.css">
  <style>
    #order1 { background: #222222; }
    #order2 { background: #222222; }
  </style>
  <link rel="stylesheet" href="/apps/linkapp/resources/second.css">
  <link rel="stylesheet" href="/system/sys.css">
  <link rel="icon" href="icon.css">
  <link rel="alternate stylesheet" href="alt.css">
  <link rel="stylesheet" href="off.css" disabled>
  <link rel="stylesheet" href="nothing.css">
  <link rel="stylesheet" href="https://example.com/net.css">
  <link rel="StyleSheet" href="upper.css"/>
  <link rel="stylesheet" href="print.css" media="print">
  <link rel="stylesheet" href="screen.css" media="screen and (min-width: 100px)">
  <ui default="/main">
    <page id="main">
      <label id="rel1" x="0" y="0">relative</label>
      <label id="order1" x="0" y="30">link, then style</label>
      <label id="order2" x="0" y="60">style, then link</label>
      <label id="sys" x="0" y="90">system</label>
      <label id="icon" x="0" y="120">icon</label>
      <label id="alt" x="0" y="150">alternate</label>
      <label id="off" x="0" y="180">disabled</label>
      <label id="net" x="0" y="210">network</label>
      <label id="upper" x="0" y="240">StyleSheet</label>
      <label id="print" x="200" y="0">media="print"</label>
      <label id="screen" x="200" y="30">media="screen and (min-width: 100px)"</label>
      <link rel="stylesheet" href="page.css">
      <label id="after" x="0" y="270">after the page link</label>
    </page>
    <page id="html">
      <link rel="stylesheet" href="flow.css">
      <p id="para">styled by flow.css</p>
    </page>
  </ui>
</app>
)BAX";

static void put(const char* path, const char* css) {
    const std::string text(css);
    LittleFS.files[path] = std::vector<uint8_t>(text.begin(), text.end());
}

#define TEST(name) printf("  %-55s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static int passed = 0;
static int total = 0;

static void bg(const char* name, const char* id, uint32_t want, bool styled = true) {
    TEST(name);
    auto* w = LvglMock::g_screen ? LvglMock::g_screen->findById(id) : nullptr;
    const bool ok = w && (styled ? (w->hasBgcolor && w->bgcolor == want) : !(w->hasBgcolor && w->bgcolor == want));
    if (ok) {
        PASS();
        return;
    }
    FAIL("");
    if (w)
        printf("      got 0x%06x hasBgcolor=%d\n", w->bgcolor, w->hasBgcolor);
    else
        printf("      no #%s\n", id);
}

int main() {
    printf("=== <link rel=\"stylesheet\"> Tests ===\n\n");

    const char* res = "/apps/linkapp/resources/";
    put((std::string(res) + "first.css").c_str(), "#rel1 { background: #0000aa; }\n#order1 { background: #aa0000; }");
    put((std::string(res) + "second.css").c_str(), "#order2 { background: #00aa00; }");
    put("/system/sys.css", "#sys { background: #123456; }");
    put((std::string(res) + "icon.css").c_str(), "#icon { background: #ff0000; }");
    put((std::string(res) + "alt.css").c_str(), "#alt { background: #ff0000; }");
    put((std::string(res) + "off.css").c_str(), "#off { background: #ff0000; }");
    put((std::string(res) + "upper.css").c_str(), "#upper { background: #abcdef; }");
    put((std::string(res) + "print.css").c_str(), "#print { background: #ff0000; }");
    put((std::string(res) + "screen.css").c_str(), "#screen { background: #c0ffee; }");
    put((std::string(res) + "page.css").c_str(), "#after { background: #0a0a0a; }");
    put((std::string(res) + "flow.css").c_str(), "#para { background: #0b0b0b; }");

    LvglMock::create_screen(480, 480);
    g_core.initDynamicApp("/apps/linkapp");
    const int count = g_core.render(APP);
    printf("Rendered %d widgets\n\n", count);

    printf("In <app>:\n");
    bg("href without / — from the app's resources/", "rel1", 0x0000aa);
    bg("<link> before <style>: the <style> wins", "order1", 0x222222);
    bg("<link> after <style>: the link wins", "order2", 0x00aa00);
    bg("href from /: a file of the watch", "sys", 0x123456);
    bg("rel=\"StyleSheet\": rel is read in any case", "upper", 0xabcdef);
    bg("media the screen fits", "screen", 0xc0ffee);

    printf("\nLeft out:\n");
    bg("rel=\"icon\"", "icon", 0xff0000, false);
    bg("rel=\"alternate stylesheet\"", "alt", 0xff0000, false);
    bg("disabled", "off", 0xff0000, false);
    bg("media=\"print\"", "print", 0xff0000, false);
    TEST("no file, an address of the network: the rest is drawn");
    if (count > 0 && LvglMock::g_screen->findById("net"))
        PASS();
    else
        FAIL("");

    printf("\nOn a page:\n");
    bg("a TelaOS page: for what comes after the link", "after", 0x0a0a0a);
    bg("an HTML page: <p> after the link", "para", 0x0b0b0b);

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d LINK TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d LINK TESTS PASSED ===\n", passed, total);
    return 1;
}
