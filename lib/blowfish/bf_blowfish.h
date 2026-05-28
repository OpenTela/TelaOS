#ifndef TELA_BF_BLOWFISH_H
#define TELA_BF_BLOWFISH_H

/*
 * Standalone Blowfish (CBC) — vendored from mbedTLS 2.28 (Apache-2.0).
 *
 * Why vendored: ESP-IDF's precompiled mbedTLS does not enable Blowfish
 * (deprecated module) and the flag can't be flipped in a prebuilt lib. This copy
 * is self-contained so it works on-device regardless of sdkconfig, and is the
 * standard Blowfish that legacy/OpenSSL files expect. All identifiers are
 * isolated (BF_/bf_/tela_) so the module never collides with a real mbedTLS in
 * the same translation unit.
 *
 * Low-level primitive. File-format framing (headers, salts, checksums, padding)
 * belongs in the Lua codec layer, not here.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BF_C                 1
#define BF_ENCRYPT           1
#define BF_DECRYPT           0
#define BF_MAX_KEY_BITS      448
#define BF_MIN_KEY_BITS      32
#define BF_ROUNDS            16
#define BF_BLOCKSIZE         8
#define BF_MODE_CBC          /* only CBC is needed */

#define BF_ERR_BAD_INPUT       -0x0016
#define BF_ERR_INVALID_LENGTH  -0x0018

#define BF_GET_UINT32_BE(data, offset)                       \
    (((uint32_t) (data)[(offset)]     << 24) |               \
     ((uint32_t) (data)[(offset) + 1] << 16) |               \
     ((uint32_t) (data)[(offset) + 2] <<  8) |               \
     ((uint32_t) (data)[(offset) + 3]))

#define BF_PUT_UINT32_BE(n, data, offset)                            \
    do {                                                             \
        (data)[(offset)]     = (unsigned char) ((n) >> 24);          \
        (data)[(offset) + 1] = (unsigned char) ((n) >> 16);          \
        (data)[(offset) + 2] = (unsigned char) ((n) >>  8);          \
        (data)[(offset) + 3] = (unsigned char) ((n));                \
    } while (0)

#define BF_BYTE_0(x) ((uint8_t) ((x) & 0xff))

#define BF_VALIDATE_RET(cond, ret) do { if (!(cond)) return (ret); } while (0)
#define BF_VALIDATE(cond)          do { if (!(cond)) return; } while (0)

static inline void bf_zeroize(void *buf, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *) buf;
    while (len--) *p++ = 0;
}

typedef struct tela_blowfish_context {
    uint32_t P[BF_ROUNDS + 2];
    uint32_t S[4][256];
} tela_blowfish_context;

void tela_blowfish_init(tela_blowfish_context *ctx);
void tela_blowfish_free(tela_blowfish_context *ctx);
int  tela_blowfish_setkey(tela_blowfish_context *ctx,
                          const unsigned char *key, unsigned int keybits);
int  tela_blowfish_crypt_ecb(tela_blowfish_context *ctx, int mode,
                             const unsigned char input[BF_BLOCKSIZE],
                             unsigned char output[BF_BLOCKSIZE]);
int  tela_blowfish_crypt_cbc(tela_blowfish_context *ctx, int mode, size_t length,
                             unsigned char iv[BF_BLOCKSIZE],
                             const unsigned char *input, unsigned char *output);

#ifdef __cplusplus
}
#endif

#endif /* TELA_BF_BLOWFISH_H */
