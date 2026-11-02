// sim_png.cpp — PNG writer on top of libpng.
#include "sim_png.h"

#include <png.h>

#include <csetjmp>
#include <cstdio>

namespace Sim {

bool writePng(const std::string& path, int w, int h, const uint8_t* rgb) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png ? png_create_info_struct(png) : nullptr;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(f);
        return false;
    }

    png_init_io(png, f);
    png_set_IHDR(png, info, w, h, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    for (int y = 0; y < h; ++y) {
        png_write_row(png, const_cast<png_bytep>(rgb + static_cast<size_t>(y) * w * 3));
    }
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    std::fclose(f);
    return true;
}

}  // namespace Sim
