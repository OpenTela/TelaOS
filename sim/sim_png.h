// sim_png.h — tiny PNG writer for simulator screenshots.
#pragma once

#include <cstdint>
#include <string>

namespace Sim {

// Writes 8-bit RGB pixels (w*h*3 bytes, top row first).
// No timestamps are stored, so identical frames give byte-identical files
// (golden screenshots can be compared by hash).
bool writePng(const std::string& path, int w, int h, const uint8_t* rgb);

}  // namespace Sim
