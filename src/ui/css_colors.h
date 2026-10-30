/**
 * css_colors.h — CSS named colors (CSS Color Level 4, all 148): "teal", "rebeccapurple"...
 *
 * One table for every color parser: HTML attributes (parse_color), CSS (Css::parseColorValue),
 * inline styles (StyleParser::parseColor).
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace UI {

// Case-insensitive lookup; false if name is not a CSS color name
bool cssNamedColor(std::string_view name, uint32_t& rgb);

}  // namespace UI
