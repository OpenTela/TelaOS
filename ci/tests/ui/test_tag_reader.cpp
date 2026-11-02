/**
 * test_tag_reader.cpp — UI::TagReader, the typed reader the page walker reads pages with.
 *
 * The rules are the walker's, kept as they were: start tags with their attributes, comments
 * and end tags passed over, content up to the end tag (counted for nested containers), void
 * and self-closing tags without content, and ends implied by the next sibling (<li>a<li>b).
 */

#include <cstdio>
#include <string>
#include <string_view>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_tag_reader.h"

#define TEST(name) printf("  %-60s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) do { printf("✗ %s\n", msg); } while(0)

using UI::StartTag;
using UI::TagId;
using UI::TagReader;
using Step = TagReader::Step;

static std::string str(std::string_view v) { return std::string(v.data(), v.size()); }

// The next start tag, passing over comments and end tags as the walker does
static bool nextTag(TagReader& r, StartTag& t) {
    for (;;) {
        const Step s = r.next(t);
        if (s == Step::Tag) return true;
        if (s == Step::End) return false;
    }
}

int main() {
    printf("=== TAG READER ===\n\n");
    int passed = 0, total = 0;
    char why[160];

    TEST("start tag: lower-case name, id, attributes") {
        TagReader r(R"(<Label id="a" x=4>Hi</label>)");
        StartTag t;
        const Step s = r.next(t);
        if (s == Step::Tag && t.name == "label" && t.id == TagId::Label && str(t.attrs.text()) == R"( id="a" x=4)")
            PASS();
        else { snprintf(why, sizeof(why), "step %d name '%s' attrs '%s'", (int)s, t.name.c_str(), str(t.attrs.text()).c_str()); FAIL(why); }
    }

    TEST("attributes: quoted, unquoted, absent") {
        TagReader r(R"(<button id="ok" w=120 onclick='go'>)");
        StartTag t;
        r.next(t);
        if (t.attrs.get("id") == "ok" && t.attrs.get("w") == "120" && t.attrs.get("onclick") == "go" &&
            t.attrs.get("x").empty())
            PASS();
        else FAIL(t.attrs.get("id").c_str());
    }

    TEST("tag ids: TelaOS tags, page and tab, everything else") {
        const bool ok = UI::tagId("div") == TagId::Div && UI::tagId("td") == TagId::Td &&
                        UI::tagId("markdown") == TagId::Markdown && UI::tagId("page") == TagId::Page &&
                        UI::tagId("tab") == TagId::Tab && UI::tagId("p") == TagId::Other &&
                        UI::tagId("table") == TagId::Table && UI::tagId("tabs") == TagId::Tabs &&
                        UI::tagId("my-widget") == TagId::Other && UI::tagId("") == TagId::Other;
        if (ok) PASS(); else FAIL("wrong id");
    }

    TEST("content up to the end tag; the reader goes past it") {
        TagReader r("<label>Hello</label><button>B</button>");
        StartTag t;
        r.next(t);
        const std::string c = str(r.content(t));
        StartTag u;
        const bool next = nextTag(r, u);
        if (c == "Hello" && next && u.id == TagId::Button && str(r.content(u)) == "B") PASS();
        else { snprintf(why, sizeof(why), "content '%s', next %s", c.c_str(), next ? u.name.c_str() : "-"); FAIL(why); }
    }

    TEST("a comment may hold '>' and tags: passed over whole") {
        TagReader r("<!-- <label>x</label> > --><button>B</button>");
        StartTag t;
        const Step first = r.next(t);
        const Step second = r.next(t);
        if (first == Step::Skip && second == Step::Tag && t.id == TagId::Button) PASS();
        else FAIL(t.name.c_str());
    }

    TEST("end tags and <!DOCTYPE> are passed over one at a time") {
        TagReader r("</div><!DOCTYPE html>text<p>");
        StartTag t;
        const Step a = r.next(t), b = r.next(t);
        const std::string rest = str(r.rest());
        const Step c = r.next(t);
        if (a == Step::Skip && b == Step::Skip && rest == "text<p>" && c == Step::Tag && t.name == "p") PASS();
        else { snprintf(why, sizeof(why), "steps %d %d %d, rest '%s'", (int)a, (int)b, (int)c, rest.c_str()); FAIL(why); }
    }

    TEST("a lone '<' is passed over") {
        TagReader r("a < b <label>L</label>");
        StartTag t;
        const Step a = r.next(t);
        const Step b = r.next(t);
        if (a == Step::Skip && b == Step::Tag && t.id == TagId::Label) PASS();
        else FAIL("lone '<' taken for a tag");
    }

    TEST("a start tag without '>' ends the page") {
        TagReader r(R"(<label>A</label><button id="b")");
        StartTag t;
        nextTag(r, t);
        r.content(t);
        if (!nextTag(r, t) && r.done()) PASS();
        else FAIL(t.name.c_str());
    }

    TEST("nested divs: the end tag that closes this one") {
        TagReader r("<div><div>in</div><label>L</label></div><button>B</button>");
        StartTag t;
        r.next(t);
        const std::string c = str(r.content(t));
        StartTag u;
        nextTag(r, u);
        if (c == "<div>in</div><label>L</label>" && u.id == TagId::Button) PASS();
        else { snprintf(why, sizeof(why), "content '%s'", c.c_str()); FAIL(why); }
    }

    TEST("HTML containers nest too (<section>)") {
        TagReader r("<section><section>a</section>b</section>c");
        StartTag t;
        r.next(t);
        const std::string c = str(r.content(t));
        if (c == "<section>a</section>b" && str(r.rest()) == "c") PASS();
        else { snprintf(why, sizeof(why), "content '%s'", c.c_str()); FAIL(why); }
    }

    TEST("void elements: no content, the reader stays after '>'") {
        TagReader r("<br>after</br><img src=a.png>x");
        StartTag t;
        r.next(t);
        const std::string c = str(r.content(t));
        const std::string rest = str(r.rest());
        if (c.empty() && rest == "after</br><img src=a.png>x") PASS();
        else { snprintf(why, sizeof(why), "content '%s', rest '%s'", c.c_str(), rest.c_str()); FAIL(why); }
    }

    TEST("<input type=...> is void, TelaOS <input> has content") {
        TagReader r(R"(<input type="checkbox">x</input><input id="name">Ann</input>)");
        StartTag a, b;
        r.next(a);
        const std::string ca = str(r.content(a));
        nextTag(r, b);
        const std::string cb = str(r.content(b));
        if (ca.empty() && cb == "Ann") PASS();
        else { snprintf(why, sizeof(why), "'%s' / '%s'", ca.c_str(), cb.c_str()); FAIL(why); }
    }

    TEST("self-closing: no content") {
        TagReader r(R"(<label id="bar"/><label id="dir">D</label>)");
        StartTag t;
        r.next(t);
        const bool selfClosing = t.attrs.selfClosing();
        const std::string c = str(r.content(t));
        StartTag u;
        nextTag(r, u);
        if (selfClosing && c.empty() && u.attrs.get("id") == "dir" && str(r.content(u)) == "D") PASS();
        else FAIL("self-closing tag swallowed its sibling");
    }

    TEST("implied end: <li>a<li>b") {
        TagReader r("<li>one<li>two</li>");
        StartTag t;
        r.next(t);
        const std::string first = str(r.content(t));
        nextTag(r, t);
        const std::string second = str(r.content(t));
        if (first == "one" && second == "two") PASS();
        else { snprintf(why, sizeof(why), "'%s' / '%s'", first.c_str(), second.c_str()); FAIL(why); }
    }

    TEST("implied end: <td>1<td>2 is not the later </td>") {
        TagReader r("<td>1<td>2</td>");
        StartTag t;
        r.next(t);
        const std::string c = str(r.content(t));
        if (c == "1") PASS();
        else { snprintf(why, sizeof(why), "content '%s'", c.c_str()); FAIL(why); }
    }

    TEST("missing end tag: no content, what follows is read as siblings") {
        TagReader r("<label>A<button>B</button>");
        StartTag t;
        r.next(t);
        const std::string c = str(r.content(t));
        const std::string rest = str(r.rest());
        if (c.empty() && rest == "A<button>B</button>") PASS();
        else { snprintf(why, sizeof(why), "content '%s', rest '%s'", c.c_str(), rest.c_str()); FAIL(why); }
    }

    TEST("the reader stays inside its text (a view of a larger page)") {
        const std::string page = "<label>A</label></page><label>outside</label>";
        TagReader r(std::string_view(page).substr(0, 16));  // <label>A</label>
        StartTag t;
        r.next(t);
        const std::string c = str(r.content(t));
        if (c == "A" && !nextTag(r, t)) PASS();
        else FAIL("read past the end of its text");
    }

    TEST("an end tag that runs past its text does not count") {
        const std::string page = "<label>A</label>";
        TagReader r(std::string_view(page).substr(0, 9));  // <label>A<
        StartTag t;
        r.next(t);
        if (r.content(t).empty()) PASS();
        else FAIL("took an end tag beyond its text");
    }

    TEST("names longer than 31 characters are cut") {
        const std::string page = "<" + std::string(40, 'x') + ">";
        TagReader r(page);
        StartTag t;
        r.next(t);
        if (t.name.size() == TagReader::kMaxName && t.id == TagId::Other) PASS();
        else { snprintf(why, sizeof(why), "name of %d", (int)t.name.size()); FAIL(why); }
    }

    printf("\n=== %s %d/%d TAG READER TESTS ===\n", passed == total ? "ALL" : "FAILED:", passed, total);
    return passed == total ? 0 : 1;
}
