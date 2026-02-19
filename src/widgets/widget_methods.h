#pragma once
/**
 * widget_methods.h - Type-safe widget operations
 * 
 * Template functions for widget manipulation.
 * Compile-time safety: if widget doesn't have the method, you get a clear error.
 * 
 * Note: C++20 concepts/requires not available in ESP-IDF toolchain (GCC 8.4),
 * so we use simple templates. Wrong type = compile error from missing method.
 */

#include <cstdint>
#include <lvgl.h>

namespace UI {

// ============================================
// Base methods - all widgets have these
// If W doesn't have setX(), you get compile error here
// ============================================

template<typename W>
void setX(W&& w, int32_t v) { w.setX(v); }

template<typename W>
void setY(W&& w, int32_t v) { w.setY(v); }

template<typename W>
void setWidth(W&& w, int32_t v) { w.setWidth(v); }

template<typename W>
void setHeight(W&& w, int32_t v) { w.setHeight(v); }

template<typename W>
void setBgColor(W&& w, uint32_t color) { w.setBgColor(color); }

template<typename W>
void setColor(W&& w, uint32_t color) { w.setColor(color); }

template<typename W>
void setVisible(W&& w, bool v) { w.setVisible(v); }

// ============================================
// Specialized methods
// If W doesn't have setText(), compile error from w.setText()
// ============================================

template<typename W>
void setText(W&& w, const char* text) { w.setText(text); }

template<typename W>
void setIcon(W&& w, const char* path) { w.setIcon(path); }

template<typename W>
void setValue(W&& w, int32_t val) { w.setValue(val); }

// ============================================
// Getters
// ============================================

template<typename W>
int32_t getX(W&& w) { return w.getX(); }

template<typename W>
int32_t getY(W&& w) { return w.getY(); }

template<typename W>
int32_t getWidth(W&& w) { return w.getWidth(); }

template<typename W>
int32_t getHeight(W&& w) { return w.getHeight(); }

template<typename W>
bool isVisible(W&& w) { return w.isVisible(); }

template<typename W>
const char* getText(W&& w) { return w.getText(); }

template<typename W>
int32_t getValue(W&& w) { return w.getValue(); }

} // namespace UI
