#pragma once
/**
 * widget_markdown.h — Markdown renderer widget using lv_spangroup
 *
 * Parses a subset of Markdown and renders via LVGL spans:
 *   # H1        → font 48
 *   ## H2       → font 32
 *   ### H3+     → font 16 (bold color)
 *   **bold**    → accent color
 *   *italic*    → dimmed color
 *   `code`      → code color
 *   - list item → "  • item"
 *   empty line  → paragraph break
 *   plain text  → default font/color
 *
 * Usage:
 *   Markdown md;
 *   md.create(parent, width);
 *   md.render("# Hello\nSome **bold** text");
 */

#include "widgets/widget_common.h"
#include "utils/font.h"
#include "utils/log_config.h"

#if LV_USE_SPAN

struct Markdown : Widget {
    // Visual config
    uint32_t    color       = 0xFFFFFF;   // default text color
    uint32_t    h1Color     = 0xFFFFFF;   // header 1 color
    uint32_t    h2Color     = 0xDDDDDD;   // header 2 color
    uint32_t    accentColor = 0x4FC3F7;   // bold text color
    uint32_t    dimColor    = 0xAAAAAA;   // italic text color
    uint32_t    codeColor   = 0x81C784;   // code text color
    uint32_t    bgcolor     = NO_COLOR;

    // Layout
    lv_align_t  align    = LV_ALIGN_TOP_LEFT;
    int         x = 0, y = 0, w = 0, h = 0;

    // ---- Lifecycle ----

    Markdown& create(lv_obj_t* parent) {
        handle = lv_spangroup_create(parent);
        lv_spangroup_set_mode(handle, LV_SPAN_MODE_BREAK);
        lv_spangroup_set_overflow(handle, LV_SPAN_OVERFLOW_CLIP);
        lv_obj_set_style_text_font(handle, Font::get(UI::Font::SMALL), 0);
        lv_obj_set_style_text_color(handle, lv_color_hex(color), 0);

        if (bgcolor != NO_COLOR) {
            applyBgColor(bgcolor);
        }

        // Transparent background by default
        lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(handle, 0, 0);
        lv_obj_set_style_pad_all(handle, 0, 0);

        applyLayout(align, x, y, w, h);

        // Enable scrolling
        lv_obj_add_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(handle, LV_SCROLLBAR_MODE_OFF);

        // Auto height by default, but if no h given, use remaining space
        if (h <= 0) {
            // Fill from y to bottom of parent
            lv_obj_set_height(handle, LV_SIZE_CONTENT);
        }

        return *this;
    }

    // ---- Render markdown text ----

    void render(const P::String& markdown) {
        if (!handle) return;

        // Clear existing spans
        clearSpans();

        if (markdown.empty()) {
            lv_spangroup_refr_mode(handle);
            return;
        }

        // Parse line by line
        size_t pos = 0;
        bool firstBlock = true;

        while (pos < markdown.size()) {
            // Find end of line
            size_t eol = markdown.find('\n', pos);
            if (eol == P::String::npos) eol = markdown.size();

            P::String line(markdown.data() + pos, eol - pos);
            pos = eol + 1;

            OS_LOGD("md", "line[%d]: len=%d [%s]", (int)(pos), (int)line.size(), line.c_str());

            // Empty line = paragraph break (visual gap)
            if (line.empty() || isAllWhitespace(line)) {
                addSpan("\n\n", Font::get(UI::Font::SMALL), color);
                firstBlock = true;
                continue;
            }

            // Add newline between consecutive lines within same block
            if (!firstBlock) {
                addSpan("\n", Font::get(UI::Font::SMALL), color);
            }
            firstBlock = false;

            // Check line type
            if (line.size() >= 4 && line[0] == '#' && line[1] == '#' && line[2] == '#' && line[3] == ' ') {
                // ### H3
                P::String text(line.data() + 4, line.size() - 4);
                addSpan(text, Font::get(UI::Font::SMALL), h2Color);
            }
            else if (line.size() >= 3 && line[0] == '#' && line[1] == '#' && line[2] == ' ') {
                // ## H2
                P::String text(line.data() + 3, line.size() - 3);
                addSpan(text, Font::get(UI::Font::MEDIUM), h2Color);
            }
            else if (line.size() >= 2 && line[0] == '#' && line[1] == ' ') {
                // # H1
                P::String text(line.data() + 2, line.size() - 2);
                addSpan(text, Font::get(UI::Font::LARGE), h1Color);
            }
            else if (line.size() >= 2 && (line[0] == '-' || (line[0] == '*' && line[1] == ' ')) && line[1] == ' ') {
                // - list item or * list item
                P::String text(line.data() + 2, line.size() - 2);
                addSpan("  \xE2\x80\xA2 ", Font::get(UI::Font::SMALL), dimColor); // bullet •
                parseInline(text);
            }
            else if (isOrderedList(line)) {
                // 1. numbered list item
                size_t dotPos = line.find(". ");
                if (dotPos != P::String::npos && dotPos + 2 < line.size()) {
                    P::String num(line.data(), dotPos + 2); // "1. "
                    P::String text(line.data() + dotPos + 2, line.size() - dotPos - 2);
                    OS_LOGD("md", "OL: num=[%s] text=[%s] len=%d", num.c_str(), text.c_str(), (int)text.size());
                    addSpan("  ", Font::get(UI::Font::SMALL), color);
                    addSpan(num, Font::get(UI::Font::SMALL), dimColor);
                    if (!text.empty()) {
                        addSpan(text, Font::get(UI::Font::SMALL), color);
                    }
                }
            }
            else {
                // Regular paragraph — parse inline formatting
                parseInline(line);
            }
        }

        lv_spangroup_refr_mode(handle);
    }

private:

    void clearSpans() {
        if (!handle) return;
        // Delete all spans
        while (lv_spangroup_get_span_count(handle) > 0) {
            lv_span_t* span = lv_spangroup_get_child(handle, 0);
            if (span) {
                lv_spangroup_delete_span(handle, span);
            } else {
                break;
            }
        }
    }

    void addSpan(const P::String& text, const lv_font_t* font, uint32_t clr) {
        if (!handle || text.empty()) return;
        lv_span_t* span = lv_spangroup_new_span(handle);
        if (!span) return;
        lv_span_set_text(span, text.c_str());
        lv_style_t* style = lv_span_get_style(span);
        if (font) {
            lv_style_set_text_font(style, font);
        }
        lv_style_set_text_color(style, lv_color_hex(clr));
    }

    // Parse inline markdown: **bold**, *italic*, `code`
    void parseInline(const P::String& text) {
        size_t i = 0;
        P::String buf;

        while (i < text.size()) {
            // **bold**
            if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
                // Flush buffer
                flushBuf(buf);
                i += 2;
                // Find closing **
                size_t end = text.find("**", i);
                if (end != P::String::npos) {
                    P::String bold(text.data() + i, end - i);
                    addSpan(bold, Font::get(UI::Font::SMALL), accentColor);
                    i = end + 2;
                } else {
                    buf += "**"; // no closing, keep literal
                }
            }
            // *italic* (single *)
            else if (text[i] == '*' && (i + 1 >= text.size() || text[i + 1] != '*')) {
                flushBuf(buf);
                i += 1;
                size_t end = text.find('*', i);
                if (end != P::String::npos) {
                    P::String italic(text.data() + i, end - i);
                    addSpan(italic, Font::get(UI::Font::SMALL), dimColor);
                    i = end + 1;
                } else {
                    buf += '*';
                }
            }
            // `code`
            else if (text[i] == '`') {
                flushBuf(buf);
                i += 1;
                size_t end = text.find('`', i);
                if (end != P::String::npos) {
                    P::String code(text.data() + i, end - i);
                    addSpan(code, Font::get(UI::Font::SMALL), codeColor);
                    i = end + 1;
                } else {
                    buf += '`';
                }
            }
            else {
                buf += text[i++];
            }
        }
        flushBuf(buf);
    }

    void flushBuf(P::String& buf) {
        if (!buf.empty()) {
            addSpan(buf, Font::get(UI::Font::SMALL), color);
            buf.clear();
        }
    }

    static bool isAllWhitespace(const P::String& s) {
        for (size_t i = 0; i < s.size(); i++) {
            if (!std::isspace(static_cast<unsigned char>(s[i]))) return false;
        }
        return true;
    }

    // Check if line is "N. text" (ordered list)
    static bool isOrderedList(const P::String& line) {
        size_t i = 0;
        if (i >= line.size() || !std::isdigit(static_cast<unsigned char>(line[i]))) return false;
        while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) i++;
        return (i + 1 < line.size() && line[i] == '.' && line[i + 1] == ' ');
    }
};

#endif // LV_USE_SPAN
