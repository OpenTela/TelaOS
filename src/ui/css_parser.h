#pragma once
/**
 * css_parser.h - Simple CSS parser for embedded UI
 * 
 * Supports selectors:
 *   tag          { ... }   — all widgets of this type
 *   .class       { ... }   — widgets with class="class"
 *   tag.class    { ... }   — widgets of type with class
 *   #id          { ... }   — widget with id="id"
 *
 * Cascade order (low → high specificity):
 *   tag < .class < tag.class < #id
 */

#include "utils/psram_alloc.h"
#include <string_view>
#include <cstdint>

// Forward declaration
struct Widget;

namespace UI {

enum class SelectorType { TAG, CLASS, TAG_CLASS, ID };

class Css {
public:
    // Parse CSS from <style> content
    void parse(std::string_view css);
    
    // Clear all rules
    void clear();
    
    // Apply all matching rules to widget (cascade: tag < .class < tag.class < #id)
    void applyMatching(Widget& w, const char* tag, const char* id, const char* classNames) const;
    
    // --- Legacy class-only API (backward compat) ---
    const P::Map<P::String, P::String>& get(const P::String& className) const;
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

private:
    struct Rule {
        SelectorType type;
        P::String tag;        // for TAG and TAG_CLASS
        P::String className;  // for CLASS and TAG_CLASS
        P::String id;         // for ID
        P::Map<P::String, P::String> properties;
    };
    
    P::Array<Rule> m_rules;
    static const P::Map<P::String, P::String> s_empty;
    static const P::String s_emptyStr;
    
    // Find first CLASS rule by name (legacy compat)
    const Rule* findClass(const P::String& className) const;
    
    // Check if rule matches widget
    bool matches(const Rule& rule, const char* tag, const char* id, const char* classNames) const;
    
    // Apply one rule's properties to widget
    void applyProps(Widget& w, const P::Map<P::String, P::String>& props) const;
    
    // Parsing helpers
    static uint32_t parseColorValue(std::string_view color);
    static int parseSizeValue(std::string_view size);
};

} // namespace UI
