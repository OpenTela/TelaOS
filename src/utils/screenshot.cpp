#include "utils/screenshot.h"
#include "hal/device.h"
#include "utils/log_config.h"
#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <lz4.h>

static const char* TAG = "Screenshot";

namespace Screenshot {

// BLE chunk data size for tiny mode target
static const int TARGET_SIZE = 248;

// Parse color format string
static ColorFormat parseColorFormat(const char* color) {
    if (strcmp(color, "rgb8") == 0) return COLOR_RGB8;
    if (strcmp(color, "gray") == 0) return COLOR_GRAY;
    if (strcmp(color, "bw") == 0) return COLOR_BW;
    if (strcmp(color, "pal") == 0) return COLOR_PALETTE;
    return COLOR_RGB16;
}

// Convert RGB565 pixels to target format
static uint32_t convertPixels(uint16_t* src, uint8_t* dst, uint32_t pixelCount, ColorFormat format) {
    switch (format) {
        case COLOR_RGB16:
            memcpy(dst, src, pixelCount * 2);
            return pixelCount * 2;
            
        case COLOR_RGB8:
            for (uint32_t i = 0; i < pixelCount; i++) {
                uint16_t p = src[i];
                uint8_t r = (p >> 11) & 0x1F;
                uint8_t g = (p >> 5) & 0x3F;
                uint8_t b = p & 0x1F;
                dst[i] = ((r >> 2) << 5) | ((g >> 3) << 2) | (b >> 3);
            }
            return pixelCount;
            
        case COLOR_GRAY:
            for (uint32_t i = 0; i < pixelCount; i++) {
                uint16_t p = src[i];
                uint8_t r = ((p >> 11) & 0x1F) << 3;
                uint8_t g = ((p >> 5) & 0x3F) << 2;
                uint8_t b = (p & 0x1F) << 3;
                dst[i] = (r * 77 + g * 150 + b * 29) >> 8;
            }
            return pixelCount;
            
        case COLOR_BW:
            for (uint32_t i = 0; i < pixelCount; i += 8) {
                uint8_t byte = 0;
                for (int bit = 0; bit < 8 && (i + bit) < pixelCount; bit++) {
                    uint16_t p = src[i + bit];
                    uint8_t r = ((p >> 11) & 0x1F) << 3;
                    uint8_t g = ((p >> 5) & 0x3F) << 2;
                    uint8_t b = (p & 0x1F) << 3;
                    uint8_t gray = (r * 77 + g * 150 + b * 29) >> 8;
                    if (gray > 127) byte |= (1 << (7 - bit));
                }
                dst[i / 8] = byte;
            }
            return (pixelCount + 7) / 8;
            
        case COLOR_PALETTE:
            break;  // handled by convertPalette()
    }
    return 0;
}

// Convert RGB565 pixels to palette-indexed format
// Output: [1:count][N*2:palette RGB565][pixels: 4bpp if N<=16, 8bpp if N<=256]
// Returns total output size, or 0 if too many colors (>256)
static uint32_t convertPalette(uint16_t* src, uint8_t* dst, uint32_t pixelCount) {
    // Phase 1: scan unique colors (linear search, fast for ≤256)
    uint16_t palette[256];
    uint32_t paletteSize = 0;
    
    // Temp buffer for indices (freed before return)
    uint8_t* indices = (uint8_t*)heap_caps_malloc(pixelCount, MALLOC_CAP_SPIRAM);
    if (!indices) {
        LOG_E(Log::UI, "Palette: failed to allocate indices buffer");
        return 0;
    }
    
    for (uint32_t i = 0; i < pixelCount; i++) {
        uint16_t color = src[i];
        
        // Find in palette
        uint32_t idx = 0;
        for (; idx < paletteSize; idx++) {
            if (palette[idx] == color) break;
        }
        
        if (idx == paletteSize) {
            // New color
            if (paletteSize >= 256) {
                LOG_D(Log::UI, "Palette: >256 unique colors, fallback to rgb16");
                heap_caps_free(indices);
                return 0;  // too many colors
            }
            palette[paletteSize++] = color;
        }
        
        indices[i] = (uint8_t)idx;
    }
    
    LOG_D(Log::UI, "Palette: %lu unique colors", paletteSize);
    
    // Phase 2: write output
    uint32_t pos = 0;
    
    // Header: palette count
    dst[pos++] = (uint8_t)paletteSize;
    
    // Palette entries (RGB565, 2 bytes each, little-endian)
    for (uint32_t i = 0; i < paletteSize; i++) {
        dst[pos++] = palette[i] & 0xFF;
        dst[pos++] = (palette[i] >> 8) & 0xFF;
    }
    
    // Pixel indices
    if (paletteSize <= 16) {
        // 4bpp packed: high nibble = first pixel
        for (uint32_t i = 0; i < pixelCount; i += 2) {
            uint8_t hi = indices[i];
            uint8_t lo = (i + 1 < pixelCount) ? indices[i + 1] : 0;
            dst[pos++] = (hi << 4) | lo;
        }
    } else {
        // 8bpp: one byte per pixel
        for (uint32_t i = 0; i < pixelCount; i++) {
            dst[pos++] = indices[i];
        }
    }
    
    heap_caps_free(indices);
    return pos;
}

// Downscale with averaging (smooth, for lossy formats)
static void downscaleImage(uint16_t* src, uint16_t* dst, 
                           uint32_t srcW, uint32_t srcH, uint32_t scale) {
    uint32_t dstW = srcW / scale;
    uint32_t dstH = srcH / scale;
    
    for (uint32_t y = 0; y < dstH; y++) {
        for (uint32_t x = 0; x < dstW; x++) {
            uint32_t rSum = 0, gSum = 0, bSum = 0;
            
            for (uint32_t dy = 0; dy < scale; dy++) {
                for (uint32_t dx = 0; dx < scale; dx++) {
                    uint16_t p = src[(y * scale + dy) * srcW + (x * scale + dx)];
                    rSum += (p >> 11) & 0x1F;
                    gSum += (p >> 5) & 0x3F;
                    bSum += p & 0x1F;
                }
            }
            
            uint32_t count = scale * scale;
            dst[y * dstW + x] = ((rSum / count) << 11) | ((gSum / count) << 5) | (bSum / count);
        }
    }
}

// Downscale nearest-neighbor (preserves exact colors for palette mode)
static void downscaleNearest(uint16_t* src, uint16_t* dst, 
                             uint32_t srcW, uint32_t srcH, uint32_t scale) {
    uint32_t dstW = srcW / scale;
    uint32_t dstH = srcH / scale;
    uint32_t half = scale / 2;
    
    for (uint32_t y = 0; y < dstH; y++) {
        for (uint32_t x = 0; x < dstW; x++) {
            dst[y * dstW + x] = src[(y * scale + half) * srcW + (x * scale + half)];
        }
    }
}

bool capture(CaptureResult& out, const char* mode, int scale, const char* color) {
    ColorFormat colorFmt = parseColorFormat(color);
    bool tinyMode = (strcmp(mode, "tiny") == 0);
    
    LOG_I(Log::UI, "Capture: mode=%s, scale=%d, color=%s", mode, scale, color);
    
    // Allocate full framebuffer
    uint32_t fullSize = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color_t);
    
    uint8_t* fullBuffer = (uint8_t*)heap_caps_malloc(fullSize, MALLOC_CAP_SPIRAM);
    if (!fullBuffer) {
        LOG_E(Log::UI, "Failed to allocate full buffer");
        return false;
    }
    
    // Capture snapshot
    #if LV_USE_SNAPSHOT
    lv_draw_buf_t draw_buf;
    lv_draw_buf_init(&draw_buf, SCREEN_WIDTH, SCREEN_HEIGHT, LV_COLOR_FORMAT_RGB565,
                     LV_STRIDE_AUTO, fullBuffer, fullSize);
    
    lv_obj_t* scr = lv_scr_act();
    lv_result_t res = lv_snapshot_take_to_draw_buf(scr, LV_COLOR_FORMAT_RGB565, &draw_buf);
    
    if (res != LV_RESULT_OK) {
        LOG_E(Log::UI, "Snapshot failed: %d", res);
        heap_caps_free(fullBuffer);
        return false;
    }
    #else
    LOG_E(Log::UI, "LV_USE_SNAPSHOT not enabled!");
    heap_caps_free(fullBuffer);
    return false;
    #endif
    
    // Allocate work buffers
    uint32_t maxDownscaledSize = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color_t);
    
    uint8_t* downscaledBuffer = (uint8_t*)heap_caps_malloc(maxDownscaledSize, MALLOC_CAP_SPIRAM);
    uint8_t* colorBuffer = (uint8_t*)heap_caps_malloc(maxDownscaledSize, MALLOC_CAP_SPIRAM);
    int maxCompressedSize = LZ4_compressBound(maxDownscaledSize);
    uint8_t* compressedBuffer = (uint8_t*)heap_caps_malloc(maxCompressedSize, MALLOC_CAP_SPIRAM);
    int lz4StateSize = LZ4_sizeofState();
    void* lz4State = heap_caps_malloc(lz4StateSize, MALLOC_CAP_SPIRAM);
    
    if (!downscaledBuffer || !colorBuffer || !compressedBuffer || !lz4State) {
        LOG_E(Log::UI, "Failed to allocate work buffers");
        if (lz4State) heap_caps_free(lz4State);
        if (compressedBuffer) heap_caps_free(compressedBuffer);
        if (colorBuffer) heap_caps_free(colorBuffer);
        if (downscaledBuffer) heap_caps_free(downscaledBuffer);
        heap_caps_free(fullBuffer);
        return false;
    }
    
    uint32_t outW, outH;
    int compressedSize = 0;
    uint32_t colorSize = 0;
    const char* actualColor = color;  // may change on palette fallback
    
    // Helper lambda: convert pixels with palette fallback
    auto doConvert = [&](uint16_t* pixels, uint32_t count) -> uint32_t {
        if (colorFmt == COLOR_PALETTE) {
            uint32_t palSize = convertPalette(pixels, colorBuffer, count);
            if (palSize > 0) {
                actualColor = "pal";
                return palSize;
            }
            // Fallback: too many colors
            LOG_D(Log::UI, "Palette fallback -> rgb16");
            actualColor = "rgb16";
            return convertPixels(pixels, colorBuffer, count, COLOR_RGB16);
        }
        return convertPixels(pixels, colorBuffer, count, colorFmt);
    };
    
    // Select downscale: nearest for palette (preserves exact colors), averaging for others
    auto downscale = (colorFmt == COLOR_PALETTE) ? downscaleNearest : downscaleImage;
    
    if (tinyMode) {
        for (uint32_t s = 2; s <= 32; s++) {
            outW = SCREEN_WIDTH / s;
            outH = SCREEN_HEIGHT / s;
            
            downscale((uint16_t*)fullBuffer, (uint16_t*)downscaledBuffer,
                          SCREEN_WIDTH, SCREEN_HEIGHT, s);
            
            colorSize = doConvert((uint16_t*)downscaledBuffer, outW * outH);
            
            compressedSize = LZ4_compress_fast_extState(
                lz4State,
                (const char*)colorBuffer, 
                (char*)compressedBuffer,
                colorSize, 
                maxCompressedSize,
                1
            );
            
            LOG_D(Log::UI, "Tiny scale %lu: %lux%lu -> %lu bytes -> LZ4 %d bytes", 
                     s, outW, outH, colorSize, compressedSize);
            
            if (compressedSize > 0 && compressedSize <= TARGET_SIZE) {
                LOG_D(Log::UI, "Fits in one frame!");
                break;
            }
        }
    } else {
        if (scale <= 1) {
            outW = SCREEN_WIDTH;
            outH = SCREEN_HEIGHT;
            colorSize = doConvert((uint16_t*)fullBuffer, outW * outH);
        } else {
            outW = SCREEN_WIDTH / scale;
            outH = SCREEN_HEIGHT / scale;
            
            downscale((uint16_t*)fullBuffer, (uint16_t*)downscaledBuffer,
                          SCREEN_WIDTH, SCREEN_HEIGHT, scale);
            
            colorSize = doConvert((uint16_t*)downscaledBuffer, outW * outH);
        }
        
        compressedSize = LZ4_compress_fast_extState(
            lz4State,
            (const char*)colorBuffer, 
            (char*)compressedBuffer,
            colorSize, 
            maxCompressedSize,
            1
        );
        
        LOG_D(Log::UI, "Fixed scale %d: %lux%lu -> %lu bytes -> LZ4 %d bytes", 
                 scale, outW, outH, colorSize, compressedSize);
    }
    
    // Free work buffers (keep compressedBuffer — it becomes the output)
    heap_caps_free(lz4State);
    heap_caps_free(colorBuffer);
    heap_caps_free(downscaledBuffer);
    heap_caps_free(fullBuffer);
    
    if (compressedSize <= 0) {
        LOG_E(Log::UI, "LZ4 compression failed");
        heap_caps_free(compressedBuffer);
        return false;
    }
    
    // Fill result — caller takes ownership of compressedBuffer
    out.buffer = compressedBuffer;
    out.size = compressedSize;
    out.rawSize = colorSize;
    out.width = outW;
    out.height = outH;
    out.color = actualColor;
    
    LOG_I(Log::UI, "Captured: %lux%lu, %d bytes (raw %lu)", outW, outH, compressedSize, colorSize);
    return true;
}

} // namespace Screenshot
