/**
 * ui_canvas.cpp - Canvas drawing API for UI::Engine
 * 
 * Extracted from ui_html.cpp — pixel-level drawing on canvas widgets.
 */

#include "lvgl.h"
#include "ui/ui_engine.h"
#include "ui/ui_html_internal.h"
#include "utils/log_config.h"

static const char* TAG = "ui_canvas";

namespace UI {

// ============ Helpers ============

static Element* findCanvas(const char* id) {
    for (auto& elem : elements) {
        if (elem->is_canvas && elem->id == id) {
            return elem.get();
        }
    }
    LOG_W(Log::UI, "Canvas not found: %s", id);
    return nullptr;
}

// ============ Canvas API ============

bool Engine::canvasClear(const char* id, uint32_t color) {
    auto* elem = findCanvas(id);
    if (!elem || !elem->canvasBuffer) return false;
    
    uint32_t argb = rgb_to_native(color);
    
    LOG_I(Log::UI, "canvasClear: id=%s color=0x%06X argb=0x%08X", id, (unsigned)color, (unsigned)argb);
    
    uint32_t* buf = (uint32_t*)elem->canvasBuffer;
    int total = elem->canvasWidth * elem->canvasHeight;
    for (int i = 0; i < total; i++) {
        buf[i] = argb;
    }
    
    lv_obj_invalidate(elem->obj());
    return true;
}

bool Engine::canvasRect(const char* id, int x, int y, int w, int h, uint32_t color) {
    auto* elem = findCanvas(id);
    if (!elem || !elem->canvasBuffer) {
        LOG_W(Log::UI, "canvasRect: canvas '%s' not found", id);
        return false;
    }
    
    // Clamp to canvas bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > elem->canvasWidth) w = elem->canvasWidth - x;
    if (y + h > elem->canvasHeight) h = elem->canvasHeight - y;
    if (w <= 0 || h <= 0) return true;
    
    uint32_t argb = rgb_to_native(color);
    
    uint32_t* buf = (uint32_t*)elem->canvasBuffer;
    int stride = elem->canvasWidth;
    
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            buf[row * stride + col] = argb;
        }
    }
    
    lv_obj_invalidate(elem->obj());
    return true;
}

bool Engine::canvasPixel(const char* id, int x, int y, uint32_t color) {
    auto* elem = findCanvas(id);
    if (!elem || !elem->canvasBuffer) return false;
    
    if (x < 0 || x >= elem->canvasWidth || y < 0 || y >= elem->canvasHeight) {
        return true; // Out of bounds - silently ignore
    }
    
    uint32_t argb = rgb_to_native(color);
    
    uint32_t* buf = (uint32_t*)elem->canvasBuffer;
    buf[y * elem->canvasWidth + x] = argb;
    
    lv_obj_invalidate(elem->obj());
    return true;
}

bool Engine::canvasCircle(const char* id, int cx, int cy, int r, uint32_t color) {
    auto* elem = findCanvas(id);
    if (!elem || !elem->canvasBuffer) return false;
    
    uint32_t argb = rgb_to_native(color);
    
    uint32_t* buf = (uint32_t*)elem->canvasBuffer;
    int w = elem->canvasWidth;
    int h = elem->canvasHeight;
    
    // Midpoint circle algorithm - filled
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    buf[py * w + px] = argb;
                }
            }
        }
    }
    
    lv_obj_invalidate(elem->obj());
    return true;
}

bool Engine::canvasLine(const char* id, int x1, int y1, int x2, int y2, uint32_t color, int thickness) {
    auto* elem = findCanvas(id);
    if (!elem || !elem->canvasBuffer) return false;
    
    uint32_t argb = rgb_to_native(color);
    
    uint32_t* buf = (uint32_t*)elem->canvasBuffer;
    int w = elem->canvasWidth;
    int h = elem->canvasHeight;
    int radius = thickness / 2;
    
    // Bresenham's line algorithm
    int dx = abs(x2 - x1);
    int dy = -abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    
    while (true) {
        int bx1 = x1 - radius;
        int by1 = y1 - radius;
        int bx2 = x1 + radius;
        int by2 = y1 + radius;
        
        if (bx1 < 0) bx1 = 0;
        if (by1 < 0) by1 = 0;
        if (bx2 >= w) bx2 = w - 1;
        if (by2 >= h) by2 = h - 1;
        
        for (int row = by1; row <= by2; row++) {
            uint32_t* rowPtr = &buf[row * w + bx1];
            for (int col = bx1; col <= bx2; col++) {
                *rowPtr++ = argb;
            }
        }
        
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
    
    lv_obj_invalidate(elem->obj());
    return true;
}

bool Engine::canvasRefresh(const char* id) {
    auto* elem = findCanvas(id);
    if (!elem) return false;
    
    lv_obj_invalidate(elem->obj());
    return true;
}

} // namespace UI
