/**
 * ui_tag_reader.cpp — see ui_tag_reader.h.
 *
 * The reading rules are the page walker's, unchanged: same tag names, same end-tag search,
 * same void and implied-end rules (those come from the HTML flow layer, ui_html_flow.h).
 */
#include "ui/ui_tag_reader.h"

#include "ui/ui_html_flow.h"
#include "ui/ui_types.h"
#include "ui/xml_utils.h"

namespace UI {
namespace {

constexpr size_t npos = std::string_view::npos;

// The "C" locale's isspace and tolower: ASCII only, as the firmware runs
constexpr bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

constexpr char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

struct NamedTag {
    std::string_view name;
    TagId id;
};

const NamedTag kTags[] = {
    {Tag::Element::Label, TagId::Label},       {Tag::Element::Button, TagId::Button},
    {Tag::Element::Switch, TagId::Switch},     {Tag::Element::Slider, TagId::Slider},
    {Tag::Element::Input, TagId::Input},       {Tag::Element::Canvas, TagId::Canvas},
    {Tag::Element::Image, TagId::Image},       {Tag::Element::Markdown, TagId::Markdown},
    {Tag::Element::Qr, TagId::Qr},             {Tag::Element::Tabs, TagId::Tabs},
    {Tag::Element::Select, TagId::Select},     {Tag::Element::List, TagId::List},
    {Tag::Element::Div, TagId::Div},           {Tag::Element::Table, TagId::Table},
    {Tag::Element::Tr, TagId::Tr},             {Tag::Element::Td, TagId::Td},
    {Tag::Layout::Page, TagId::Page},          {Tag::Element::Tab, TagId::Tab},
};

// `text` has `part` at `pos`
bool at(std::string_view text, size_t pos, std::string_view part) {
    return pos <= text.size() && text.size() - pos >= part.size() && text.compare(pos, part.size(), part) == 0;
}

// `text` has </name> at `pos`
bool endTagAt(std::string_view text, size_t pos, std::string_view name) {
    return at(text, pos, "</") && at(text, pos + 2, name) && at(text, pos + 2 + name.size(), ">");
}

}  // namespace

TagId tagId(std::string_view name) {
    for (const NamedTag& t : kTags) {
        if (t.name == name) return t.id;
    }
    return TagId::Other;
}

P::String Attrs::get(const char* name) const { return XmlUtils::getAttr(begin(), end(), name); }

bool Attrs::has(std::string_view name) const {
    const std::string_view s = text_;
    size_t p = 0;
    while (p < s.size()) {
        while (p < s.size() && (isSpace(s[p]) || s[p] == '/')) ++p;
        const size_t start = p;
        while (p < s.size() && s[p] != '=' && !isSpace(s[p]) && s[p] != '/') ++p;
        if (p - start == name.size() && !name.empty()) {
            bool same = true;
            for (size_t i = 0; i < name.size() && same; ++i) same = lowerAscii(s[start + i]) == lowerAscii(name[i]);
            if (same) return true;
        }
        while (p < s.size() && isSpace(s[p])) ++p;
        if (p < s.size() && s[p] == '=') {  // pass over the value
            ++p;
            while (p < s.size() && isSpace(s[p])) ++p;
            if (p < s.size() && (s[p] == '"' || s[p] == '\'')) {
                const char quote = s[p++];
                while (p < s.size() && s[p] != quote) ++p;
                if (p < s.size()) ++p;
            } else {
                while (p < s.size() && !isSpace(s[p])) ++p;
            }
        }
    }
    return false;
}

TagReader::Step TagReader::next(StartTag& tag) {
    const size_t n = html_.size();
    size_t p = html_.find('<', pos_);
    if (p == npos) {
        pos_ = n;
        return Step::End;
    }
    ++p;
    if (at(html_, p, "!--")) {  // a comment may contain '>'
        const size_t close = html_.find("-->", p + 3);
        pos_ = close == npos ? n : close + 3;
        return Step::Skip;
    }
    if (p < n && (html_[p] == '/' || html_[p] == '!')) {  // an end tag, <!DOCTYPE ...>
        const size_t gt = html_.find('>', p);
        pos_ = gt == npos ? n : gt + 1;
        return Step::Skip;
    }

    tag.name.clear();
    size_t q = p;
    while (q < n && !isSpace(html_[q]) && html_[q] != '>' && html_[q] != '/' && tag.name.size() < kMaxName) {
        tag.name += lowerAscii(html_[q++]);
    }
    if (tag.name.empty()) {  // a lone '<'
        pos_ = p;
        return Step::Skip;
    }
    const size_t gt = html_.find('>', q);
    if (gt == npos) {
        pos_ = n;
        return Step::End;
    }
    tag.id = tagId(tag.name);
    tag.attrs = Attrs(html_.substr(q, gt - q));
    pos_ = gt + 1;
    return Step::Tag;
}

std::string_view TagReader::content(const StartTag& tag) {
    const char* const name = tag.name.c_str();
    // HTML void elements (<br>, <hr>, <img>, <input type=...>) have no end tag; TelaOS <input>
    // without type= keeps its own rule
    const bool htmlVoid = HtmlFlow::isVoid(name) &&
                          (tag.id != TagId::Input || HtmlFlow::isHtmlInput(tag.attrs.begin(), tag.attrs.end()));
    if (tag.attrs.selfClosing() || htmlVoid) return {};

    const size_t start = pos_;
    // <div> and HTML containers nest: their end tag is found counting the inner ones
    const size_t close = endTag(tag.name, tag.id == TagId::Div || HtmlFlow::isNestable(name));
    // <td>, <tr>, <li>, <p> may omit their end tag: such an element ends at the next sibling
    // that implies it (<td>1<td>2), not at a later </td> of another cell
    if (HtmlFlow::hasOptionalEnd(name)) {
        const char* const base = html_.data();
        const char* const implied = HtmlFlow::impliedEnd(name, base + start, base + html_.size());
        if (implied && (close == npos || static_cast<size_t>(implied - base) < close)) {
            pos_ = static_cast<size_t>(implied - base);
            return html_.substr(start, pos_ - start);
        }
    }
    if (close == npos) return {};
    pos_ = close + tag.name.size() + 3;  // past </name>
    return html_.substr(start, close - start);
}

size_t TagReader::endTag(std::string_view name, bool nested) const {
    if (!nested) {  // the first </name>
        for (size_t p = html_.find("</", pos_); p != npos; p = html_.find("</", p + 1)) {
            if (endTagAt(html_, p, name)) return p;
        }
        return npos;
    }
    int depth = 1;
    size_t p = pos_;
    while (p < html_.size()) {
        if (endTagAt(html_, p, name)) {
            if (--depth == 0) return p;
            p += name.size() + 3;
        } else if (html_[p] == '<' && at(html_, p + 1, name)) {
            const size_t after = p + 1 + name.size();
            const char c = after < html_.size() ? html_[after] : '\0';
            if (c == '>' || c == ' ' || c == '/') ++depth;  // <name ...> opens one more
            ++p;
        } else {
            ++p;
        }
    }
    return npos;
}

}  // namespace UI
