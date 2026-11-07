#include "core/build_info.h"

// Written into the build folder by scripts/build_id.py; the only file that includes it
#if __has_include("tela_build_id.h")
#include "tela_build_id.h"
#endif
#ifndef TELA_BUILD_ID
#define TELA_BUILD_ID "dev"
#endif

namespace BuildInfo {

const char* id() { return TELA_BUILD_ID; }

} // namespace BuildInfo
