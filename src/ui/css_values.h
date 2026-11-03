#pragma once
/**
 * css_values.h — CSS values as the watch reads them: numbers, lengths, colors. What each property
 * takes: docs/CSS.md. A value that does not parse makes its declaration invalid, and the caller
 * leaves it out, as a browser does.
 */

#include "utils/psram_alloc.h"
#include <cstdint>
#include <string_view>

namespace UI {

constexpr int kCssRemPx = 16;  // the root font size: 1rem, as in a browser

// A number at s[i] (12, -0.5, .5, 1e3); i moves past it
bool cssNumber(std::string_view s, size_t& i, float& v);

// The space-separated parts of a value; parentheses and quotes keep a part whole:
// "4px var(--a, 1px 2px)" has two parts
P::Array<std::string_view> cssParts(std::string_view value);

// A length in pixels: a number (px), px, em (em px each), rem, vw and vh (1% of the screen).
// false for anything else, % too: what % is of depends on the property.
bool cssLength(std::string_view text, float em, int32_t& px);

// A font size in pixels: a length or a percentage, both of the parent's font size (parentPx), as
// font-size takes them; 0 if it does not read or is not positive
int cssFontSize(std::string_view text, float parentPx);

// A color and its alpha
struct CssColor {
    uint32_t rgb = 0;     // 0xRRGGBB
    uint8_t alpha = 255;  // 0 transparent ... 255 opaque
};

// #RGB, #RGBA, #RRGGBB, #RRGGBBAA, the 148 names, transparent, rgb()/rgba() and hsl()/hsla() with
// commas or spaces (rgb(0 128 255 / 50%)); false for anything else
bool cssColor(std::string_view text, CssColor& out);

}  // namespace UI
