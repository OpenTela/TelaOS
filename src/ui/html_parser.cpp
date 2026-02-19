/**
 * html_parser.cpp - Modern C++ HTML/XML parser implementation
 */

#include "ui/html_parser.h"
#include "ui/ui_types.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace UI {

using namespace Tag;

// ============ ATTRIBUTE ============

int Attribute::toInt(int def) const {
    if (value.empty()) return def;
    return std::atoi(value.c_str());
}

float Attribute::toFloat(float def) const {
    if (value.empty()) return def;
    return static_cast<float>(std::atof(value.c_str()));
}

bool Attribute::toBool(bool def) const {
    if (value.empty()) return def;
    char c = value[0];
    return c == 't' || c == 'T' || c == '1' || c == 'y' || c == 'Y';
}

// ============ ELEMENT ============

bool ParsedElement::has(std::string_view name) const {
    return attrs.find(P::String(name.data(), name.size())) != attrs.end();
}

std::string_view ParsedElement::get(std::string_view name, std::string_view def) const {
    auto it = attrs.find(P::String(name.data(), name.size()));
    if (it != attrs.end()) {
        return it->second;
    }
    return def;
}

int ParsedElement::getInt(std::string_view name, int def) const {
    auto it = attrs.find(P::String(name.data(), name.size()));
    if (it != attrs.end() && !it->second.empty()) {
        return std::atoi(it->second.c_str());
    }
    return def;
}

float ParsedElement::getFloat(std::string_view name, float def) const {
    auto it = attrs.find(P::String(name.data(), name.size()));
    if (it != attrs.end() && !it->second.empty()) {
        return static_cast<float>(std::atof(it->second.c_str()));
    }
    return def;
}

bool ParsedElement::getBool(std::string_view name, bool def) const {
    auto it = attrs.find(P::String(name.data(), name.size()));
    if (it != attrs.end() && !it->second.empty()) {
        char c = it->second[0];
        return c == 't' || c == 'T' || c == '1' || c == 'y' || c == 'Y';
    }
    return def;
}

const ParsedElement* ParsedElement::find(std::string_view t) const {
    for (const auto& child : children) {
        if (child.tag == t) return &child;
    }
    return nullptr;
}

std::vector<const ParsedElement*> ParsedElement::findAll(std::string_view t) const {
    std::vector<const ParsedElement*> result;
    for (const auto& child : children) {
        if (child.tag == t) result.push_back(&child);
    }
    return result;
}

// ============ PARSER CONTEXT ============

void Parser::ParseContext::skipWs() {
    while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) {
        pos++;
    }
}

// ============ PARSER ============

ParsedElement Parser::parse(std::string_view html) {
    ParseContext ctx{html, 0};
    ctx.skipWs();
    
    ParsedElement root;
    root.tag = "root";
    
    while (!ctx.eof()) {
        ctx.skipWs();
        if (ctx.eof()) break;
        
        if (ctx.peek() == '<') {
            // Check for comment
            if (ctx.remaining().substr(0, 4) == "<!--") {
                skipComment(ctx);
                continue;
            }
            
            // Check for closing tag at root level (shouldn't happen)
            if (ctx.remaining().size() > 1 && ctx.remaining()[1] == '/') {
                break;
            }
            
            ParsedElement child = parseElement(ctx);
            if (!child.empty()) {
                root.children.push_back(std::move(child));
            }
        } else {
            ctx.skip();
        }
    }
    
    return root;
}

void Parser::forEach(std::string_view html, 
                     std::function<void(const ParsedElement&)> callback) {
    ParsedElement root = parse(html);
    for (const auto& child : root.children) {
        callback(child);
    }
}

std::optional<ParsedElement> Parser::findSection(std::string_view html, 
                                            std::string_view tag) {
    ParsedElement root = parse(html);
    
    // Recursive search helper
    std::function<const ParsedElement*(const ParsedElement&)> findRecursive = 
        [&](const ParsedElement& el) -> const ParsedElement* {
        for (const auto& child : el.children) {
            if (child.tag == tag) {
                return &child;
            }
            if (auto* found = findRecursive(child)) {
                return found;
            }
        }
        return nullptr;
    };
    
    if (auto* found = findRecursive(root)) {
        return *found;
    }
    return std::nullopt;
}

std::string_view Parser::extractSection(std::string_view html, 
                                         std::string_view tag) {
    // Find <tag
    P::String openTag = P::String("<") + P::String(tag.data(), tag.size()).c_str();
    size_t start = html.find(openTag.c_str());
    if (start == std::string_view::npos) return "";
    
    // Find >
    size_t contentStart = html.find('>', start);
    if (contentStart == std::string_view::npos) return "";
    contentStart++;
    
    // Find </tag>
    P::String closeTag = P::String("</") + P::String(tag.data(), tag.size()).c_str() + ">";
    size_t end = html.find(closeTag.c_str(), contentStart);
    if (end == std::string_view::npos) return "";
    
    return html.substr(contentStart, end - contentStart);
}

ParsedElement Parser::parseElement(ParseContext& ctx) {
    ParsedElement elem;
    
    if (ctx.peek() != '<') return elem;
    ctx.skip(); // <
    
    // Parse tag name
    elem.tag = parseTagName(ctx);
    if (elem.tag.empty()) return elem;
    
    // Parse attributes
    elem.attrs = parseAttrs(ctx);
    
    ctx.skipWs();
    
    // Self-closing tag?
    if (ctx.peek() == '/') {
        ctx.skip(); // /
        if (ctx.peek() == '>') ctx.skip(); // >
        return elem;
    }
    
    if (ctx.peek() == '>') {
        ctx.skip(); // >
    }
    
    // Special handling for script/style - take raw content until closing tag
    if (elem.tag == Meta::Script || elem.tag == Meta::Style) {
        elem.text = ctx.extractUntilClose(elem.tag);
        return elem;
    }
    
    // Parse content
    while (!ctx.eof()) {
        ctx.skipWs();
        
        if (ctx.peek() == '<') {
            // Check for comment
            if (ctx.remaining().substr(0, 4) == "<!--") {
                skipComment(ctx);
                continue;
            }
            
            // Closing tag?
            if (ctx.remaining().size() > 1 && ctx.remaining()[1] == '/') {
                // Skip </tag>
                size_t closeEnd = ctx.remaining().find('>');
                if (closeEnd != std::string_view::npos) {
                    ctx.skip(closeEnd + 1);
                }
                break;
            }
            
            // Nested element
            ParsedElement child = parseElement(ctx);
            if (!child.empty()) {
                elem.children.push_back(std::move(child));
            }
        } else {
            // Text content
            P::String text = parseText(ctx);
            if (!text.empty()) {
                elem.text += text.c_str();
            }
        }
    }
    
    return elem;
}

P::String Parser::parseTagName(ParseContext& ctx) {
    P::String name;
    ctx.skipWs();
    
    while (!ctx.eof()) {
        char c = ctx.peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':') {
            name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ctx.skip();
        } else {
            break;
        }
    }
    
    return name;
}

P::Map<P::String, P::String> Parser::parseAttrs(ParseContext& ctx) {
    P::Map<P::String, P::String> attrs;
    
    while (!ctx.eof()) {
        ctx.skipWs();
        
        char c = ctx.peek();
        if (c == '>' || c == '/' || c == '\0') break;
        
        // Parse attribute name
        P::String name;
        while (!ctx.eof()) {
            c = ctx.peek();
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':') {
                name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                ctx.skip();
            } else {
                break;
            }
        }
        
        if (name.empty()) {
            ctx.skip();
            continue;
        }
        
        ctx.skipWs();
        
        // Check for =
        if (ctx.peek() == '=') {
            ctx.skip();
            ctx.skipWs();
            attrs[name] = parseAttrValue(ctx);
        } else {
            // Boolean attribute
            attrs[name] = "true";
        }
    }
    
    return attrs;
}

P::String Parser::parseAttrValue(ParseContext& ctx) {
    P::String value;
    
    char quote = ctx.peek();
    if (quote == '"' || quote == '\'') {
        ctx.skip(); // Opening quote
        
        while (!ctx.eof() && ctx.peek() != quote) {
            value += ctx.get();
        }
        
        if (ctx.peek() == quote) ctx.skip(); // Closing quote
    } else {
        // Unquoted value
        while (!ctx.eof()) {
            char c = ctx.peek();
            if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/') break;
            value += ctx.get();
        }
    }
    
    return value;
}

P::String Parser::parseText(ParseContext& ctx) {
    P::String text;
    
    while (!ctx.eof() && ctx.peek() != '<') {
        text += ctx.get();
    }
    
    // Trim
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) start++;
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) end--;
    
    if (start >= end) return "";
    return P::String(text.data() + start, end - start);
}

void Parser::skipComment(ParseContext& ctx) {
    // Skip <!--
    ctx.skip(4);
    
    // Find -->
    while (!ctx.eof()) {
        if (ctx.remaining().substr(0, 3) == "-->") {
            ctx.skip(3);
            return;
        }
        ctx.skip();
    }
}

// ============ STYLE PARSER ============

std::vector<StyleProperty> StyleParser::parse(std::string_view style) {
    std::vector<StyleProperty> props;
    
    size_t pos = 0;
    while (pos < style.size()) {
        // Skip whitespace
        while (pos < style.size() && std::isspace(static_cast<unsigned char>(style[pos]))) {
            pos++;
        }
        if (pos >= style.size()) break;
        
        // Find property name
        size_t nameStart = pos;
        while (pos < style.size() && style[pos] != ':' && style[pos] != ';') {
            pos++;
        }
        if (pos >= style.size() || style[pos] != ':') continue;
        
        P::String name(style.data() + nameStart, pos - nameStart);
        // Trim name
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) {
            name.pop_back();
        }
        
        pos++; // Skip :
        
        // Skip whitespace
        while (pos < style.size() && std::isspace(static_cast<unsigned char>(style[pos]))) {
            pos++;
        }
        
        // Find value
        size_t valueStart = pos;
        while (pos < style.size() && style[pos] != ';') {
            pos++;
        }
        
        P::String value(style.data() + valueStart, pos - valueStart);
        // Trim value
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
            value.pop_back();
        }
        
        if (!name.empty()) {
            props.push_back({std::move(name), std::move(value)});
        }
        
        if (pos < style.size() && style[pos] == ';') pos++;
    }
    
    return props;
}

P::Map<P::String, P::String> StyleParser::parseToMap(std::string_view style) {
    P::Map<P::String, P::String> result;
    for (auto& prop : parse(style)) {
        result[std::move(prop.name)] = std::move(prop.value);
    }
    return result;
}

uint32_t StyleParser::parseColor(std::string_view color) {
    if (color.empty()) return 0x000000;
    
    // Skip #
    if (color[0] == '#') color = color.substr(1);
    
    // Named colors
    if (color == "black") return 0x000000;
    if (color == "white") return 0xFFFFFF;
    if (color == "red") return 0xFF0000;
    if (color == "green") return 0x00FF00;
    if (color == "blue") return 0x0000FF;
    if (color == "yellow") return 0xFFFF00;
    if (color == "cyan") return 0x00FFFF;
    if (color == "magenta") return 0xFF00FF;
    if (color == "gray" || color == "grey") return 0x808080;
    if (color == "orange") return 0xFFA500;
    
    // Hex - use stack buffer, no heap alloc
    char buf[8];
    
    if (color.size() == 6) {
        memcpy(buf, color.data(), 6);
        buf[6] = '\0';
        return static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
    }
    
    // Short hex (#RGB -> #RRGGBB)
    if (color.size() == 3) {
        buf[0] = color[0]; buf[1] = '\0';
        uint32_t r = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        buf[0] = color[1];
        uint32_t g = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        buf[0] = color[2];
        uint32_t b = static_cast<uint32_t>(std::strtoul(buf, nullptr, 16));
        return (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
    }
    
    return 0x000000;
}

int StyleParser::parseSize(std::string_view size) {
    if (size.empty()) return 0;
    // Stack buffer for atoi
    char buf[16];
    size_t len = size.size() < 15 ? size.size() : 15;
    memcpy(buf, size.data(), len);
    buf[len] = '\0';
    return std::atoi(buf);  // Ignores "px", "%", etc.
}

// StyleProperty::toInt() and toColor() are implemented in ui_types.cpp

} // namespace UI
