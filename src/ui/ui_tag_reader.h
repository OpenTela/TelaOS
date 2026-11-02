/**
 * ui_tag_reader.h — a page read tag by tag, with types: which tag it is (TagId), its
 * attributes (Attrs), its content (TagReader::content).
 *
 * The page walker (parse_children) reads pages with it. The other places that still read
 * tags on raw pointers (templates, @for, tabs, the HTML flow layer) move onto it next, one
 * at a time, each time with every screen unchanged.
 *
 * Everything here is a view (std::string_view) into the page text: valid while that text is.
 */
#pragma once

#include "utils/psram_alloc.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace UI {

// Tags the page walker builds itself
enum class TagId : uint8_t {
    Other,  // a standard HTML element (HtmlFlow) or an unknown tag
    Label,
    Button,
    Switch,
    Slider,
    Input,
    Canvas,
    Image,
    Markdown,
    Qr,
    Tabs,
    Select,
    List,
    Div,
    Table,
    Tr,
    Td,
    Page,  // skipped by the walker: the engine builds pages
    Tab,   // skipped by the walker: <tabs> builds its tabs
};

// TagId of a lower-case tag name
TagId tagId(std::string_view name);

// Layout containers: their children are built inside them
inline bool isContainer(TagId id) {
    return id == TagId::Div || id == TagId::Table || id == TagId::Tr || id == TagId::Td;
}

// Attributes of a start tag: the text between its name and '>' (` id="a" x="4%" /`)
class Attrs {
public:
    Attrs() = default;
    explicit Attrs(std::string_view text) : text_(text) {}

    // Value of the attribute `name`; "" if it is absent or empty
    P::String get(const char* name) const;
    // <label ... />
    bool selfClosing() const { return !text_.empty() && text_.back() == '/'; }
    std::string_view text() const { return text_; }

    // For the widget builders that still read attributes from a range of characters
    const char* begin() const { return text_.data(); }
    const char* end() const { return text_.data() + text_.size(); }

private:
    std::string_view text_;
};

struct StartTag {
    TagId id = TagId::Other;
    P::String name;  // lower-case
    Attrs attrs;
};

// Reads a page from start to end. Text between tags is left to the caller (the HTML flow
// layer takes it as inline runs); comments, end tags and <!...> are passed over one at a
// time, so the caller sees the text after each of them too.
class TagReader {
public:
    enum class Step : uint8_t {
        Tag,   // a start tag: in `tag`, the reader is right after its '>'
        Skip,  // a comment, an end tag, <!...> or a lone '<' was passed over
        End,   // nothing more: the end, or a start tag without its '>'
    };

    explicit TagReader(std::string_view html) : html_(html) {}

    Step next(StartTag& tag);

    // Content of the start tag just read: up to its end tag (counting nested ones of the same
    // name in a container), or up to where its end is implied (<li>a<li>b). The reader moves
    // past it. Empty for void (<br>) and self-closing tags, and when the end tag is missing:
    // then what follows is read as siblings.
    std::string_view content(const StartTag& tag);

    // Not read yet
    std::string_view rest() const { return html_.substr(pos_); }
    bool done() const { return pos_ >= html_.size(); }
    // The caller has taken the next n characters (an inline run)
    void skip(size_t n) { pos_ = n < html_.size() - pos_ ? pos_ + n : html_.size(); }

    // Longer tag names are cut, as the walker always did (no tag is that long)
    static constexpr size_t kMaxName = 31;

private:
    // Start of the end tag of `name` after the reader, npos if there is none
    size_t endTag(std::string_view name, bool nested) const;

    std::string_view html_;
    size_t pos_ = 0;
};

}  // namespace UI
