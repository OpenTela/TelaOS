/**
 * string_utils.h - String utility functions
 */

#pragma once

#include <string>
#include <string_view>
#include "utils/psram_alloc.h"

namespace UI {
namespace StringUtils {

void trim(P::String& s);
P::String trimmed(std::string_view sv);

void toLower(P::String& s);
P::String toLowerCopy(std::string_view sv);

bool contains(std::string_view s, char c);
bool contains(std::string_view haystack, std::string_view needle);

bool toBool(const char* s);
bool toBool(std::string_view sv);

bool equals(const char* a, const char* b);
bool equals(std::string_view a, const char* b);
bool equalsIgnoreCase(std::string_view a, std::string_view b);

P::String decodeEntities(const P::String& s);

} // namespace StringUtils
} // namespace UI
