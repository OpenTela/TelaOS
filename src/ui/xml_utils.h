#pragma once

#include <string>
#include <string_view>
#include <cstddef>
#include "utils/psram_alloc.h"

namespace UI {
namespace XmlUtils {

// Tag search
const char* findTagOpen(const char* p, const char* tag);
const char* findTagClose(const char* p, const char* tag);

// Tag length helpers
size_t tagOpenLen(const char* tag);
size_t tagCloseLen(const char* tag);

// Attribute extraction
P::String getAttr(const char* start, const char* end, const char* name);
const char* getAttr(const char* start, const char* end, const char* name, char* out, size_t maxLen);
bool hasAttr(const char* start, const char* end, const char* name);
int getAttrInt(const char* start, const char* end, const char* name, int defaultVal = 0);
bool getAttrBool(const char* start, const char* end, const char* name, bool defaultVal = false);

} // namespace XmlUtils
} // namespace UI
