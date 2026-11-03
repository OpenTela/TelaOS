#pragma once
/**
 * css_media.h — @media for the watch: does a media query list hold for its screen? The screen
 * does not change while an app runs, so a sheet is read once: the rules of an @media that holds
 * are read as if they stood outside it, the others are left out. What is supported: docs/CSS.md.
 */

#include <string_view>

namespace UI {

// The screen a query is asked about: the board's (SCREEN_WIDTH × SCREEN_HEIGHT), a touch screen,
// the dark theme
struct CssScreen {
    float width = 0;   // px
    float height = 0;  // px
    float dpi = 0;
};
CssScreen cssScreen();

// The prelude of @media: `screen and (max-width: 480px), (orientation: landscape)`. True if any
// query in the list holds; a query that does not read never holds, as in a browser.
bool cssMediaMatches(std::string_view queryList, const CssScreen& screen = cssScreen());

}  // namespace UI
