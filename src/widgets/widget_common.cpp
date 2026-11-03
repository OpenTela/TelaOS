/**
 * widget_common.cpp — Widget non-inline methods
 */

#include "widgets/widget_common.h"
#include "ui/css_parser.h"
#include "ui/css_undo.h"
#include "ui/dom.h"
#include "utils/string_utils.h"
#include <cctype>

void Widget::applyStyle(const P::String& classNames) {
    if (!handle || classNames.empty()) return;
    // Legacy: class-only, no tag/id context
    UI::Css::instance().applyMatching(*this, nullptr, nullptr, classNames.c_str());
}

void Widget::applyCss(const char* tag, const char* id, const char* classNames, std::string_view attrs,
                      std::string_view inlineStyle) {
    if (!handle) return;
    P::String style;  // the style="" of a TelaOS widget: its builder gives none, its attributes have it
    if (inlineStyle.empty() && UI::Css::findAttr(attrs, "style", inlineStyle) &&
        inlineStyle.find('&') != std::string_view::npos) {
        style = UI::StringUtils::decodeEntities(P::String(inlineStyle));
        inlineStyle = style;
    }
    // the element scripts find (docs/DOM.md); what CSS changes on it is kept for a class change
    const bool dom = UI::Dom::element(handle, tag ? tag : "", attrs, true);
    UI::CssElement el;
    el.tag = tag ? tag : "";
    el.id = id ? id : "";
    el.classes = classNames ? classNames : "";
    el.attrs = attrs;
    UI::CssUndo undo;
    UI::Css::instance().applyMatching(*this, el, inlineStyle, dom ? &undo : nullptr);
    if (dom) UI::Dom::keep(std::move(undo));
}
