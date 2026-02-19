#pragma once

#include <cstdint>

/**
 * Screenshot module - capture screen to PSRAM buffer
 * 
 * Returns LZ4-compressed image. Caller owns the buffer.
 * Transport (BinTransfer) handles chunked delivery.
 */

namespace Screenshot {

enum ColorFormat {
    COLOR_RGB16 = 0,  // RGB565 - 16 bit
    COLOR_RGB8 = 1,   // RGB332 - 8 bit  
    COLOR_GRAY = 2,   // Grayscale - 8 bit
    COLOR_BW = 3,     // Black/White - 1 bit
    COLOR_PALETTE = 4  // Indexed palette - 4 or 8 bit, lossless
};

struct CaptureResult {
    uint8_t* buffer = nullptr;   // LZ4-compressed data (PSRAM, caller frees)
    uint32_t size = 0;           // compressed size
    uint32_t rawSize = 0;        // uncompressed size
    uint32_t width = 0;          // output width
    uint32_t height = 0;         // output height
    const char* color = "rgb16"; // color format name
};

/**
 * Capture screenshot to PSRAM buffer
 * 
 * @param out       result struct (buffer ownership transfers to caller)
 * @param mode      "tiny" (auto-fit one BLE frame) or "fixed" 
 * @param scale     0 = full size, 2-32 = downscale factor
 * @param color     "rgb16", "rgb8", "gray", "bw"
 * @return true on success
 */
bool capture(CaptureResult& out, const char* mode, int scale, const char* color);

} // namespace Screenshot
