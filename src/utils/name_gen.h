#pragma once

#include <cstddef>

/**
 * Name Generator & Sanitizer
 *
 * sanitize(dst, src, size):
 *   - Transliterate Cyrillic, European diacritics
 *   - Lowercase, spaces→dashes
 *   - Keep only [a-z0-9_-]
 *   - If empty → generate pronounceable name
 *
 * generate(dst, size):
 *   - Pronounceable name from syllables (CV pattern)
 *   - Example: "kobami", "zelura", "tapino"
 */
namespace NameGen {

/// Sanitize name with transliteration, fallback to generated name
void sanitize(char* dst, const char* src, size_t dstSize);

/// Generate pronounceable name (consonant-vowel pattern)
void generate(char* dst, size_t dstSize);

} // namespace NameGen
