#pragma once
/**
 * css_parser.h — CSS for the TelaOS UI: style sheets from <style> and style="", matched to the
 * widgets as they are built. What works and what does not: docs/CSS.md
 *
 * Selectors: type (button, p), *, .class, #id, [attr], [attr=v] (also ~= |= ^= $= *=, and an
 * `i` flag), any compound of them (button.primary#ok[type]), lists (h1, h2), descendant (nav a)
 * and child (ul > li) combinators. A selector with anything else (:hover, ::before, a + b) is
 * left out, as a browser leaves out a selector it does not know.
 *
 * Cascade, as in CSS: declarations go by specificity (ids; classes and attributes; types), then
 * by their order in the sheets; style="" comes after the sheets; !important after all of that,
 * in the same order, and style="" !important last.
 */

#include "utils/psram_alloc.h"
#include <string_view>
#include <cstdint>

// Forward declaration
struct Widget;

namespace UI {

// An element as a selector sees it. Views into the page text (or the caller's strings): valid
// while the element is being built.
struct CssElement {
    std::string_view tag;      // lower-case
    std::string_view id;
    std::string_view classes;  // as in class="": names separated by spaces
    std::string_view attrs;    // raw attributes of the start tag, for [attr] selectors
};

// The elements the page walkers are inside of, outermost first: descendant and child
// combinators (nav a, ul > li) match against them. A scope lives while its element's content
// is built, so an element never is its own ancestor.
class CssScope {
public:
    CssScope(std::string_view tag, std::string_view attrs);  // id and class come from attrs
    ~CssScope();
    CssScope(const CssScope&) = delete;
    CssScope& operator=(const CssScope&) = delete;

    static const P::Array<CssElement>& ancestors();
};

struct CssDecl {
    P::String name;  // lower-case (a custom property --name keeps its case)
    P::String value;
    bool important = false;
};

// Parsed selectors and rules (internal to css_parser.cpp)
namespace CssDetail {

struct AttrTest {
    P::String name;   // lower-case
    P::String value;
    char op = 0;      // 0: present, '=': equal, '~' '|' '^' '$' '*': as in [a~=v]...
    bool caseless = false;  // [a=v i]
};

struct Compound {     // button.primary#ok[type]
    P::String tag;    // lower-case; "": any (also *)
    P::String id;
    P::Array<P::String> classes;
    P::Array<AttrTest> attrs;
};

struct Selector {
    P::Array<Compound> parts;  // left to right
    P::String combinators;     // between parts: ' ' descendant, '>' child
    uint32_t specificity = 0;  // ids << 16 | (classes + attributes) << 8 | types
};

struct Rule {
    P::Array<Selector> selectors;  // h1, h2
    P::Array<CssDecl> decls;       // in source order
    uint32_t order = 0;            // the rule's place among all rules parsed
};

}  // namespace CssDetail

class Css {
public:
    // Parse a style sheet (<style> content); its rules come after the ones parsed before
    void parse(std::string_view css);

    // Clear all rules
    void clear();

    // What applies to an element, with CssScope::ancestors() as its ancestors and inlineStyle
    // as its style="": one declaration per property, the winning one, in cascade order
    P::Array<CssDecl> cascade(const CssElement& el, std::string_view inlineStyle = {}) const;

    // The cascade put on a widget
    void applyMatching(Widget& w, const CssElement& el, std::string_view inlineStyle = {}) const;
    // The same for an element known by tag, id and class only
    void applyMatching(Widget& w, const char* tag, const char* id, const char* classNames) const;

    // --- Legacy class-only API: the first rule that is just `.className` ---
    bool has(const P::String& className) const;
    const P::String& prop(const P::String& className, const P::String& property) const;
    int getInt(const P::String& className, const P::String& property, int def = 0) const;
    uint32_t getColor(const P::String& className, const P::String& property, uint32_t def = 0) const;

    // Debug: dump all rules
    void dump() const;

    // Singleton for global styles
    static Css& instance();

    // Trim whitespace (public for helpers)
    static P::String trim(std::string_view s);

    // The value of attribute `name` (any case) in raw attribute text, as written: no entities
    // decoded. false if there is no such attribute; a valueless one (checked) gives "".
    static bool findAttr(std::string_view attrs, std::string_view name, std::string_view& value);

private:
    P::Array<CssDetail::Rule> m_rules;
    uint32_t m_nextOrder = 0;
    static const P::String s_emptyStr;

    // The first rule that is just `.className` (legacy class-only API)
    const CssDetail::Rule* findClass(const P::String& className) const;

    // Apply resolved declarations to a widget
    void applyProps(Widget& w, const P::Array<CssDecl>& decls) const;

    // Parsing helpers
    static uint32_t parseColorValue(std::string_view color);
    static int parseSizeValue(std::string_view size);
};

} // namespace UI
