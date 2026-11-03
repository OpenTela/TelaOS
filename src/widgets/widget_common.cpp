/**
 * widget_common.cpp — Widget non-inline methods
 */

#include "widgets/widget_common.h"
#include "ui/css_parser.h"
#include <cctype>

void Widget::applyStyle(const P::String& classNames) {
    if (!handle || classNames.empty()) return;
    // Legacy: class-only, no tag/id context
    UI::Css::instance().applyMatching(*this, nullptr, nullptr, classNames.c_str());
}

void Widget::applyCss(const char* tag, const char* id, const char* classNames, std::string_view attrs,
                      std::string_view inlineStyle) {
    if (!handle) return;
    UI::CssElement el;
    el.tag = tag ? tag : "";
    el.id = id ? id : "";
    el.classes = classNames ? classNames : "";
    el.attrs = attrs;
    UI::Css::instance().applyMatching(*this, el, inlineStyle);
}
