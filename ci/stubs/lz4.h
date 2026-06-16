#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
inline int LZ4_compress_default(const char*, char*, int, int) { return 0; }
inline int LZ4_compressBound(int sz) { return sz + sz/255 + 16; }
inline int LZ4_sizeofState() { return 16384; }
inline int LZ4_compress_fast_extState(void*, const char*, char*, int, int, int) { return 0; }

// Minimal but real LZ4 block decompressor (handles literals + matches).
// Correct for the all-literal blocks our OTA host tests feed; defensive on
// bounds so malformed input returns < 0 like the real lib.
inline int LZ4_decompress_safe(const char* src, char* dst, int csz, int dcap) {
    const uint8_t* ip   = (const uint8_t*)src;
    const uint8_t* iend = ip + csz;
    uint8_t* op   = (uint8_t*)dst;
    uint8_t* oend = op + dcap;

    while (ip < iend) {
        uint8_t token = *ip++;

        int litlen = token >> 4;
        if (litlen == 15) {
            uint8_t s;
            do { if (ip >= iend) return -1; s = *ip++; litlen += s; } while (s == 255);
        }
        if (ip + litlen > iend || op + litlen > oend) return -1;
        memcpy(op, ip, litlen);
        op += litlen;
        ip += litlen;

        if (ip >= iend) break;            // last sequence is literals-only

        uint16_t offset = ip[0] | (ip[1] << 8);
        ip += 2;
        if (offset == 0) return -1;

        int matchlen = token & 0x0F;
        if (matchlen == 15) {
            uint8_t s;
            do { if (ip >= iend) return -1; s = *ip++; matchlen += s; } while (s == 255);
        }
        matchlen += 4;

        uint8_t* match = op - offset;
        if (match < (uint8_t*)dst || op + matchlen > oend) return -1;
        for (int i = 0; i < matchlen; i++) *op++ = *match++;
    }
    return (int)(op - (uint8_t*)dst);
}
