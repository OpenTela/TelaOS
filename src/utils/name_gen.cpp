#include "utils/name_gen.h"
#include <Arduino.h>
#include <cstring>

namespace NameGen {

// ─── Transliteration tables ───────────────────────────────────────────────

// Cyrillic lowercase (0x0430-0x044F) → Latin
static const char* CYRILLIC_LOWER[] = {
    "a", "b", "v", "g", "d", "e", "zh", "z",   // а б в г д е ж з
    "i", "j", "k", "l", "m", "n", "o", "p",    // и й к л м н о п  
    "r", "s", "t", "u", "f", "h", "c", "ch",   // р с т у ф х ц ч
    "sh", "sch", "", "y", "", "e", "yu", "ya"  // ш щ ъ ы ь э ю я
};

// Cyrillic uppercase (0x0410-0x042F) → same as lower
#define CYRILLIC_UPPER CYRILLIC_LOWER

// Ukrainian/Belarusian extras
// і (0x0456) → i, ї (0x0457) → yi, є (0x0454) → ye, ґ (0x0491) → g
// ё (0x0451) → yo, э (0x044D) → e

// European diacritics (common ones)
struct DiacriticMap {
    uint16_t code;
    char latin;
};

static const DiacriticMap DIACRITICS[] = {
    // German
    {0x00E4, 'a'}, {0x00F6, 'o'}, {0x00FC, 'u'}, {0x00DF, 's'},  // ä ö ü ß
    {0x00C4, 'a'}, {0x00D6, 'o'}, {0x00DC, 'u'},                  // Ä Ö Ü
    // French
    {0x00E0, 'a'}, {0x00E2, 'a'}, {0x00E7, 'c'}, {0x00E8, 'e'},  // à â ç è
    {0x00E9, 'e'}, {0x00EA, 'e'}, {0x00EB, 'e'}, {0x00EE, 'i'},  // é ê ë î
    {0x00EF, 'i'}, {0x00F4, 'o'}, {0x00F9, 'u'}, {0x00FB, 'u'},  // ï ô ù û
    // Spanish/Portuguese
    {0x00F1, 'n'}, {0x00E3, 'a'}, {0x00F5, 'o'},                  // ñ ã õ
    // Polish
    {0x0105, 'a'}, {0x0107, 'c'}, {0x0119, 'e'}, {0x0142, 'l'},  // ą ć ę ł
    {0x0144, 'n'}, {0x00F3, 'o'}, {0x015B, 's'}, {0x017A, 'z'},  // ń ó ś ź
    {0x017C, 'z'},                                                // ż
    // Czech/Slovak
    {0x010D, 'c'}, {0x010F, 'd'}, {0x011B, 'e'}, {0x0148, 'n'},  // č ď ě ň
    {0x0159, 'r'}, {0x0161, 's'}, {0x0165, 't'}, {0x016F, 'u'},  // ř š ť ů
    {0x017E, 'z'},                                                // ž
    // Nordic
    {0x00E5, 'a'}, {0x00F8, 'o'}, {0x00E6, 'a'},                  // å ø æ
    // Romanian  
    {0x0103, 'a'}, {0x00E2, 'a'}, {0x00EE, 'i'}, {0x0219, 's'},  // ă â î ș
    {0x021B, 't'},                                                // ț
    // Turkish
    {0x011F, 'g'}, {0x0131, 'i'}, {0x015F, 's'},                  // ğ ı ş
    // Hungarian
    {0x0151, 'o'}, {0x0171, 'u'},                                  // ő ű
    {0, 0}  // terminator
};

// ─── Syllable generator ───────────────────────────────────────────────────

static const char* CONSONANTS[] = {
    "k", "t", "p", "s", "n", "m", "r", "l", "b", "d", "g", "v", "z", "f", "h"
};
static const char* VOWELS[] = {
    "a", "e", "i", "o", "u"
};

static const int NUM_CONSONANTS = sizeof(CONSONANTS) / sizeof(CONSONANTS[0]);
static const int NUM_VOWELS = sizeof(VOWELS) / sizeof(VOWELS[0]);

// Simple PRNG (xorshift)
static uint32_t s_seed = 0;

static uint32_t nextRand() {
    if (s_seed == 0) s_seed = micros() ^ (millis() << 16);
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

void generate(char* dst, size_t dstSize) {
    if (dstSize < 4) {
        dst[0] = '\0';
        return;
    }

    // 3 syllables: CV-CV-CV (e.g., "ko-ba-mi")
    size_t pos = 0;
    for (int i = 0; i < 3 && pos < dstSize - 3; i++) {
        const char* c = CONSONANTS[nextRand() % NUM_CONSONANTS];
        const char* v = VOWELS[nextRand() % NUM_VOWELS];
        
        size_t cLen = strlen(c);
        size_t vLen = strlen(v);
        
        if (pos + cLen + vLen < dstSize - 1) {
            memcpy(dst + pos, c, cLen);
            pos += cLen;
            memcpy(dst + pos, v, vLen);
            pos += vLen;
        }
    }
    dst[pos] = '\0';
}

// ─── UTF-8 helpers ────────────────────────────────────────────────────────

// Decode one UTF-8 codepoint, return bytes consumed (1-4), or 0 on error
static int utf8Decode(const uint8_t* p, uint32_t* codepoint) {
    if ((p[0] & 0x80) == 0) {
        *codepoint = p[0];
        return 1;
    }
    if ((p[0] & 0xE0) == 0xC0) {
        *codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    if ((p[0] & 0xF0) == 0xE0) {
        *codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0) {
        *codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    return 0;  // invalid
}

// Transliterate one codepoint, return Latin string (may be multiple chars)
static const char* transliterate(uint32_t cp) {
    // ASCII lowercase
    if (cp >= 'A' && cp <= 'Z') {
        static char buf[2];
        buf[0] = cp + 32;
        buf[1] = '\0';
        return buf;
    }
    if (cp >= 'a' && cp <= 'z') {
        static char buf[2];
        buf[0] = cp;
        buf[1] = '\0';
        return buf;
    }
    if (cp >= '0' && cp <= '9') {
        static char buf[2];
        buf[0] = cp;
        buf[1] = '\0';
        return buf;
    }
    if (cp == ' ') return "-";
    if (cp == '_' || cp == '-') {
        static char buf[2];
        buf[0] = cp;
        buf[1] = '\0';
        return buf;
    }

    // Cyrillic lowercase (а-я: 0x0430-0x044F)
    if (cp >= 0x0430 && cp <= 0x044F) {
        return CYRILLIC_LOWER[cp - 0x0430];
    }
    // Cyrillic uppercase (А-Я: 0x0410-0x042F)
    if (cp >= 0x0410 && cp <= 0x042F) {
        return CYRILLIC_LOWER[cp - 0x0410];
    }
    // Ukrainian і
    if (cp == 0x0456 || cp == 0x0406) return "i";
    // Ukrainian ї
    if (cp == 0x0457 || cp == 0x0407) return "yi";
    // Ukrainian є
    if (cp == 0x0454 || cp == 0x0404) return "ye";
    // Ukrainian ґ
    if (cp == 0x0491 || cp == 0x0490) return "g";
    // Russian ё
    if (cp == 0x0451 || cp == 0x0401) return "yo";

    // European diacritics
    for (int i = 0; DIACRITICS[i].code != 0; i++) {
        if (cp == DIACRITICS[i].code) {
            static char buf[2];
            buf[0] = DIACRITICS[i].latin;
            buf[1] = '\0';
            return buf;
        }
    }

    // Unknown → skip
    return "";
}

// ─── Main sanitize function ───────────────────────────────────────────────

void sanitize(char* dst, const char* src, size_t dstSize) {
    if (dstSize == 0) return;

    const uint8_t* p = (const uint8_t*)src;
    size_t dstPos = 0;

    while (*p && dstPos < dstSize - 1) {
        uint32_t cp;
        int bytes = utf8Decode(p, &cp);
        if (bytes == 0) {
            p++;  // skip invalid byte
            continue;
        }
        p += bytes;

        const char* latin = transliterate(cp);
        size_t latinLen = strlen(latin);
        
        if (latinLen > 0 && dstPos + latinLen < dstSize - 1) {
            memcpy(dst + dstPos, latin, latinLen);
            dstPos += latinLen;
        }
    }
    dst[dstPos] = '\0';

    // Remove leading/trailing dashes, collapse multiple dashes
    // Simple cleanup pass
    size_t readPos = 0, writePos = 0;
    bool lastWasDash = true;  // treat start as "after dash" to skip leading
    while (dst[readPos]) {
        if (dst[readPos] == '-') {
            if (!lastWasDash) {
                dst[writePos++] = '-';
                lastWasDash = true;
            }
        } else {
            dst[writePos++] = dst[readPos];
            lastWasDash = false;
        }
        readPos++;
    }
    // Remove trailing dash
    if (writePos > 0 && dst[writePos - 1] == '-') {
        writePos--;
    }
    dst[writePos] = '\0';

    // If empty, generate pronounceable name
    if (writePos == 0) {
        generate(dst, dstSize);
    }
}

} // namespace NameGen
