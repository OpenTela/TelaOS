#pragma once
/**
 * html_parser.h - Modern C++ HTML/XML parser for UI engine
 * 
 * Replaces C-style strstr/strcmp parsing with clean C++17 API
 */

#include "ui/ui_engine.h"  // For StyleProperty
#include "utils/psram_alloc.h"
#include <string_view>
#include <optional>
#include <functional>
#include <cstdint>

namespace UI {

// ============ ATTRIBUTE ============

struct Attribute {
    P::String name;
    P::String value;
    
    bool empty() const { return name.empty(); }
    
    // Conversions
    int toInt(int def = 0) const;
    float toFloat(float def = 0.0f) const;
    bool toBool(bool def = false) const;
    
    bool operator==(std::string_view v) const { return value == v; }
};

// ============ ELEMENT ============

struct ParsedElement {
    P::String tag;
    P::Map<P::String, P::String> attrs;
    P::String text;                    // Text content
    P::Array<ParsedElement> children;
    
    // Attribute access
    bool has(std::string_view name) const;
    std::string_view get(std::string_view name, std::string_view def = "") const;
    int getInt(std::string_view name, int def = 0) const;
    float getFloat(std::string_view name, float def = 0.0f) const;
    bool getBool(std::string_view name, bool def = false) const;
    
    // Shorthand
    std::string_view id() const { return get("id"); }
    std::string_view cls() const { return get("class"); }
    
    // Children
    const ParsedElement* find(std::string_view tag) const;
    std::vector<const ParsedElement*> findAll(std::string_view tag) const;
    
    // Check
    bool is(std::string_view t) const { return tag == t; }
    bool empty() const { return tag.empty(); }
};

// ============ PARSER ============

class Parser {
public:
    // Parse full document
    static ParsedElement parse(std::string_view html);
    
    // Parse and iterate elements (memory efficient for large docs)
    static void forEach(std::string_view html, 
                        std::function<void(const ParsedElement&)> callback);
    
    // Find specific section
    static std::optional<ParsedElement> findSection(std::string_view html, 
                                               std::string_view tag);
    
    // Extract sections by name
    static std::string_view extractSection(std::string_view html, 
                                            std::string_view tag);

private:
    struct ParseContext {
        std::string_view src;
        size_t pos = 0;
        
        bool eof() const { return pos >= src.size(); }
        char peek() const { return eof() ? '\0' : src[pos]; }
        char get() { return eof() ? '\0' : src[pos++]; }
        void skip(size_t n = 1) { pos += n; }
        void skipWs();
        
        std::string_view remaining() const { 
            return pos < src.size() ? src.substr(pos) : ""; 
        }
        
        // Extract content until closing tag
        P::String extractUntilClose(const P::String& tag) {
            P::String closeTag = P::String("</") + tag.c_str() + ">";
            size_t closePos = remaining().find(closeTag.c_str());
            if (closePos == std::string_view::npos) return "";
            
            P::String content(remaining().data(), closePos);
            skip(closePos + closeTag.size());
            return content;
        }
    };
    
    static ParsedElement parseElement(ParseContext& ctx);
    static P::String parseTagName(ParseContext& ctx);
    static P::Map<P::String, P::String> parseAttrs(ParseContext& ctx);
    static P::String parseAttrValue(ParseContext& ctx);
    static P::String parseText(ParseContext& ctx);
    static void skipComment(ParseContext& ctx);
};

// ============ STYLE PARSER ============

class StyleParser {
public:
    static std::vector<StyleProperty> parse(std::string_view style);
    static P::Map<P::String, P::String> parseToMap(std::string_view style);
    
    // Common extractions
    static uint32_t parseColor(std::string_view color);
    static int parseSize(std::string_view size);  // "16px" -> 16
};

} // namespace UI
