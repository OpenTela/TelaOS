#pragma once
/**
 * css_parser.h — CSS for the TelaOS UI: style sheets from <style> and style="", matched to the
 * widgets as they are built. What works and what does not: docs/CSS.md
 *
 * Selectors: type (button, p), *, .class, #id, [attr], [attr=v] (also ~= |= ^= $= *=, and an
 * `i` flag), any compound of them (button.primary#ok[type]), lists (h1, h2), descendant (nav a),
 * child (ul > li) and sibling (h2 + p, h2 ~ p) combinators, :root, the place among siblings
 * (:first-child, :last-child, :only-child, :nth-child(2n+1) and the -last- and -of-type forms,
 * read from the page text), :not(), :is() and :where() with selector lists, the states LVGL
 * keeps for a widget: :active (pressed), :focus, :checked, :disabled, :hover, and ::placeholder
 * of a text field. A selector with anything else (::before) is left out, as a browser leaves out
 * a selector it does not know. @media whose query holds for the screen (css_media.h) counts as
 * if its rules stood outside it.
 *
 * Cascade, as in CSS: declarations go by specificity (ids; classes and attributes; types), then
 * by their order in the sheets; style="" comes after the sheets; !important after all of that,
 * in the same order, and style="" !important last.
 *
 * Values (css_values.h): lengths in px, em, rem, vw, vh; colors with alpha; 1 to 4 values in
 * padding; custom properties (--x), inherited, and var(--x, fallback). A value that does not read
 * leaves its declaration out, as in a browser.
 */

#include "utils/psram_alloc.h"
#include <string_view>
#include <cstdint>

// Forward declaration
struct Widget;

namespace UI {

class CssUndo;

// An element as a selector sees it. Views into the page text (or the caller's strings): valid
// while the element is being built.
struct CssElement {
    std::string_view tag;      // lower-case
    std::string_view id;
    std::string_view classes;  // as in class="": names separated by spaces
    std::string_view attrs;    // raw attributes of the start tag, for [attr] selectors
    std::string_view content;  // its content, as an ancestor: where :first-child, h2 + p read its children
    bool root = false;         // the root above every page: :root { --x: ... }
};

// The elements the page walkers are inside of, outermost first: descendant and child
// combinators (nav a, ul > li) match against them. A scope lives while its element's content
// is built, so an element never is its own ancestor. content is the text its children are built
// from: an element's place among them (li:first-child, h2 + p) is read there, found by where
// its attributes are in that text.
// An element child of a scope's element, as places and siblings read it (li:first-child, h2 + p)
struct CssChild {
    std::string_view tag;    // lower-case
    std::string_view attrs;  // its attribute text: where it is tells it from the others
};

class CssScope {
public:
    // id and class come from attrs
    CssScope(std::string_view tag, std::string_view attrs, std::string_view content = {});
    // The same, its children given instead of read from its content: an element built earlier,
    // whose text is gone (UI::Dom applies CSS again when a script changes a class)
    CssScope(std::string_view tag, std::string_view attrs, const P::Array<CssChild>& children);
    ~CssScope();
    CssScope(const CssScope&) = delete;
    CssScope& operator=(const CssScope&) = delete;

    static const P::Array<CssElement>& ancestors();

private:
    bool dom_ = false;  // the element is in the tree scripts see (UI::Dom)
};

// While it lives, the scope on top reads its children from `text` instead of `original`, if it
// held `original`: the page walker builds a page from a copy when it expands @for and components
class CssScopeText {
public:
    CssScopeText(std::string_view original, std::string_view text);
    ~CssScopeText();
    CssScopeText(const CssScopeText&) = delete;
    CssScopeText& operator=(const CssScopeText&) = delete;

private:
    bool active_ = false;
    std::string_view saved_;
};

struct CssDecl {
    P::String name;  // lower-case (a custom property --name keeps its case)
    P::String value;
    bool important = false;
};

// What wins for one state of an element: 0 is the element as it is; LV_STATE_PRESSED (:active) and
// the others are styles LVGL switches to by itself when the widget enters the state
struct CssStateDecls {
    uint16_t state = 0;
    P::Array<CssDecl> decls;
};

// Parsed selectors and rules (internal to css_parser.cpp)
namespace CssDetail {

struct AttrTest {
    P::String name;   // lower-case
    P::String value;
    char op = 0;      // 0: present, '=': equal, '~' '|' '^' '$' '*': as in [a~=v]...
    bool caseless = false;  // [a=v i]
};

// :nth-child(2n+1): the element is the (a*k + b)-th for some k >= 0, counted from the start or
// the end (:nth-last-child, :last-child), among all element siblings or those of its tag (-of-type)
struct NthTest {
    int16_t a = 0;
    int16_t b = 1;
    bool fromEnd = false;
    bool ofType = false;
};

struct Selector;

// :not(.a, .b), :is(.a, .b), :where(.a, .b): the element matches none of the list, or one of it.
// The list's selectors are matched where the element stands (:not(.menu a) looks at its ancestors)
struct Logical {
    bool negate = false;       // :not()
    P::Array<Selector> list;
};

// What a selector ends on: the element itself, or a part LVGL draws apart
enum class Pseudo : uint8_t { None, Placeholder };

// The rare parts of a compound, kept apart: most compounds (.x, p) have none of them
struct CompoundExtra {
    P::Array<NthTest> nths;         // :first-child, :nth-child()...
    P::Array<Logical> logic;        // :not(), :is(), :where()
    uint32_t logicSpecificity = 0;  // what those add: the most specific of each list (:where() adds none)
};

struct Compound {     // button.primary#ok[type]
    P::String tag;    // lower-case; "": any (also *)
    P::String id;
    P::Array<P::String> classes;
    P::Array<AttrTest> attrs;
    P::Ptr<CompoundExtra> extra;    // nullptr when it has no place tests and no :not() / :is()
    bool root = false;    // :root
    uint16_t states = 0;  // :active, :focus, :checked, :disabled, :hover as LV_STATE_* bits
    Pseudo pseudo = Pseudo::None;   // ::placeholder; only the last compound has one
    uint8_t pseudos = 0;  // pseudo-classes, for specificity
};

struct Selector {
    P::Array<Compound> parts;  // left to right
    P::String combinators;     // between parts: ' ' descendant, '>' child, '+' next sibling, '~' a later sibling
    uint32_t specificity = 0;  // ids << 16 | (classes + attributes) << 8 | types
};

struct Rule {
    P::Array<Selector> selectors;  // h1, h2
    P::Array<CssDecl> decls;       // in source order
    uint32_t order = 0;            // the rule's place among all rules parsed
};

}  // namespace CssDetail

// Selectors a script finds elements by (querySelector, UI::Dom): read as the sheets read them, but
// one that does not read makes the whole list wrong, as in a browser. An element matches with
// CssScope::ancestors() as its ancestors; a state (:checked) is the one its widget is in now
class CssQuery {
public:
    explicit CssQuery(std::string_view selectors);
    bool ok() const { return !list_.empty(); }
    // states: the LV_STATE_* bits el's widget is in
    bool matches(const CssElement& el, uint16_t states) const;

private:
    P::Array<CssDetail::Selector> list_;  // empty: the selectors do not read
};

class Css {
public:
    // Parse a style sheet (<style> content); its rules come after the ones parsed before
    void parse(std::string_view css);

    // Clear all rules
    void clear();

    // What applies to an element, with CssScope::ancestors() as its ancestors and inlineStyle
    // as its style="": one declaration per property, the winning one, in cascade order, var()s in
    // the values put in
    P::Array<CssDecl> cascade(const CssElement& el, std::string_view inlineStyle = {}) const;

    // The same for every state the sheets have rules for: [0] is the element as it is (if any
    // rule applies to it), then each state (:active...) in LV_STATE_* order
    P::Array<CssStateDecls> cascadeStates(const CssElement& el, std::string_view inlineStyle = {}) const;

    // The custom properties (--x) el declares, in its style="" too: what its content inherits
    P::Array<CssDecl> customProperties(const CssElement& el) const;

    // A selector of the sheets looks at attribute `name` (class and id too): changing it may
    // change what applies
    bool looksAt(std::string_view name) const;

    // The cascade put on a widget; undo: keeps what it changed (css_undo.h)
    void applyMatching(Widget& w, const CssElement& el, std::string_view inlineStyle = {},
                       CssUndo* undo = nullptr) const;
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

    // The declarations of a style="" (or of a rule's block), in order: names in lower case (a
    // custom property --x keeps its case), values trimmed, !important apart
    static P::Array<CssDecl> declarations(std::string_view block);

private:
    P::Array<CssDetail::Rule> m_rules;
    uint32_t m_nextOrder = 0;
    bool m_hasCustom = false;                  // a sheet declares a custom property
    bool m_hasPlaceholder = false;             // a rule for ::placeholder
    mutable P::Array<CssDecl> m_rootCustom;    // :root's custom properties, worked out once
    mutable bool m_rootReady = false;
    static const P::String s_emptyStr;

    // cascade() with explicit ancestors and their custom properties; root: :root's custom
    // properties, nullptr while they are worked out
    P::Array<CssStateDecls> cascadeIn(const CssElement& el, std::string_view inlineStyle,
                                      const P::Array<CssElement>& anc, int top,
                                      const P::Array<P::Array<CssDecl>>& ancCustom, bool withRoot,
                                      CssDetail::Pseudo pseudo = CssDetail::Pseudo::None) const;
    const P::Array<CssDecl>& rootCustom() const;

    // The first rule that is just `.className` (legacy class-only API)
    const CssDetail::Rule* findClass(const P::String& className) const;

    // Apply resolved declarations to a widget, as it is (state 0) or for a state
    void applyProps(Widget& w, const P::Array<CssDecl>& decls, uint16_t state = 0) const;

    // Parsing helpers
    static uint32_t parseColorValue(std::string_view color);
    static int parseSizeValue(std::string_view size);
};

} // namespace UI
