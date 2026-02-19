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

void Widget::applyCss(const char* tag, const char* id, const char* classNames) {
    if (!handle) return;
    UI::Css::instance().applyMatching(*this, tag, id, classNames);
}
