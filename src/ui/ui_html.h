/**
 * ui_html.h - HTML-like UI markup for LVGL
 * 
 * C++ only API via UI::Engine class.
 */

#ifndef UI_HTML_H
#define UI_HTML_H

#include "ui/ui_engine.h"

// Convenience function
inline UI::Engine& ui_engine() { 
    return UI::Engine::instance(); 
}

#endif // UI_HTML_H
