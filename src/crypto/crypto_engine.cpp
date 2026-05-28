#include "crypto/crypto_engine.h"
#include "utils/log_config.h"

#include <cstring>
#include <vector>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/sha256.h"
#include "mbedtls/md.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md5.h"

#include "bf_blowfish.h"

namespace Crypto {

static const char* TAG = "Crypto";

// ---- CSPRNG (single shared DRBG) -------------------------------------------

static mbedtls_entropy_context  s_entropy;
static mbedtls_ctr_drbg_context s_drbg;
static bool                     s_seeded = false;

bool init() {
    if (s_seeded) return true;

    mbedtls_entropy_init(&s_entropy);
    mbedtls_ctr_drbg_init(&s_drbg);

    static const char pers[] = "TelaOS-crypto-v0";
    int ret = mbedtls_ctr_drbg_seed(
        &s_drbg, mbedtls_entropy_func, &s_entropy,
        reinterpret_cast<const unsigned char*>(pers), sizeof(pers) - 1);

    if (ret != 0) {
        LOG_E(Log::CRYPTO, "ctr_drbg_seed failed: -0x%04x", -ret);
        mbedtls_ctr_drbg_free(&s_drbg);
        mbedtls_entropy_free(&s_entropy);
        return false;
    }

    s_seeded = true;
    LOG_I(Log::CRYPTO, "CSPRNG seeded");
    return true;
}

bool random(size_t n, std::string& out) {
    if (!init()) return false;
    out.resize(n);
    if (n == 0) return true;
    int ret = mbedtls_ctr_drbg_random(
        &s_drbg, reinterpret_cast<unsigned char*>(&out[0]), n);
    if (ret != 0) {
        LOG_E(Log::CRYPTO, "drbg_random failed: -0x%04x", -ret);
        out.clear();
        return false;
    }
    return true;
}

// ---- Hash / MAC ------------------------------------------------------------

bool sha256(const uint8_t* data, size_t len, uint8_t out[kSha256Len]) {
    // last arg 0 => SHA-256 (not SHA-224)
    int ret = mbedtls_sha256_ret(data, len, out, 0);
    if (ret != 0) {
        LOG_E(Log::CRYPTO, "sha256 failed: -0x%04x", -ret);
        return false;
    }
    return true;
}

bool md5(const uint8_t* data, size_t len, uint8_t out[kMd5Len]) {
    int ret = mbedtls_md5_ret(data, len, out);
    if (ret != 0) {
        LOG_E(Log::CRYPTO, "md5 failed: -0x%04x", -ret);
        return false;
    }
    return true;
}

bool hmacSha256(const uint8_t* key, size_t keyLen,
                const uint8_t* data, size_t dataLen,
                uint8_t out[kSha256Len]) {
    const mbedtls_md_info_t* info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return false;
    int ret = mbedtls_md_hmac(info, key, keyLen, data, dataLen, out);
    if (ret != 0) {
        LOG_E(Log::CRYPTO, "hmac failed: -0x%04x", -ret);
        return false;
    }
    return true;
}

bool pbkdf2Sha256(const uint8_t* password, size_t pwLen,
                  const uint8_t* salt, size_t saltLen,
                  uint32_t iterations, size_t dkLen,
                  std::string& out) {
    if (iterations == 0 || dkLen == 0) return false;

    const mbedtls_md_info_t* info =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info) return false;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    int ret = mbedtls_md_setup(&ctx, info, /*hmac=*/1);
    if (ret == 0) {
        out.resize(dkLen);
        ret = mbedtls_pkcs5_pbkdf2_hmac(
            &ctx, password, pwLen, salt, saltLen, iterations, dkLen,
            reinterpret_cast<unsigned char*>(&out[0]));
    }
    mbedtls_md_free(&ctx);

    if (ret != 0) {
        LOG_E(Log::CRYPTO, "pbkdf2 failed: -0x%04x", -ret);
        out.clear();
        return false;
    }
    return true;
}

// ---- AES-GCM ---------------------------------------------------------------

static bool validKeyLen(size_t n) { return n == 16 || n == 24 || n == 32; }

bool aesGcmEncrypt(const uint8_t* key, size_t keyLen,
                   const uint8_t* plain, size_t plainLen,
                   const uint8_t* aad, size_t aadLen,
                   std::string& out) {
    if (!validKeyLen(keyLen)) {
        LOG_E(Log::CRYPTO, "bad AES key length: %u", (unsigned)keyLen);
        return false;
    }

    std::string iv;
    if (!random(kGcmIvLen, iv)) return false;

    out.resize(kGcmIvLen + plainLen + kGcmTagLen);
    uint8_t* o = reinterpret_cast<uint8_t*>(&out[0]);
    memcpy(o, iv.data(), kGcmIvLen);
    uint8_t* ct  = o + kGcmIvLen;
    uint8_t* tag = ct + plainLen;

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                 key, (unsigned)(keyLen * 8));
    if (ret == 0) {
        ret = mbedtls_gcm_crypt_and_tag(
            &gcm, MBEDTLS_GCM_ENCRYPT, plainLen,
            reinterpret_cast<const unsigned char*>(iv.data()), kGcmIvLen,
            aad, aadLen, plain, ct, kGcmTagLen, tag);
    }
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        LOG_E(Log::CRYPTO, "gcm encrypt failed: -0x%04x", -ret);
        out.clear();
        return false;
    }
    return true;
}

bool aesGcmDecrypt(const uint8_t* key, size_t keyLen,
                   const uint8_t* blob, size_t blobLen,
                   const uint8_t* aad, size_t aadLen,
                   std::string& out) {
    if (!validKeyLen(keyLen)) return false;
    if (blobLen < kGcmIvLen + kGcmTagLen) {
        LOG_W(Log::CRYPTO, "gcm blob too short: %u", (unsigned)blobLen);
        return false;
    }

    const uint8_t* iv  = blob;
    const uint8_t* ct  = blob + kGcmIvLen;
    size_t         ctLen = blobLen - kGcmIvLen - kGcmTagLen;
    const uint8_t* tag = ct + ctLen;

    out.resize(ctLen);

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                                 key, (unsigned)(keyLen * 8));
    if (ret == 0) {
        ret = mbedtls_gcm_auth_decrypt(
            &gcm, ctLen, iv, kGcmIvLen, aad, aadLen,
            tag, kGcmTagLen, ct,
            ctLen ? reinterpret_cast<unsigned char*>(&out[0]) : nullptr);
    }
    mbedtls_gcm_free(&gcm);

    if (ret != 0) {
        // MBEDTLS_ERR_GCM_AUTH_FAILED here => tamper / wrong key. Expected path.
        LOG_D(Log::CRYPTO, "gcm auth/decrypt failed: -0x%04x", -ret);
        out.clear();
        return false;
    }
    return true;
}

// ---- Blowfish-CBC (legacy interop; vendored standalone) --------------------

bool blowfishCbc(bool encrypt,
                 const uint8_t* key, size_t keyLen,
                 const uint8_t iv[kBlowfishBlock],
                 const uint8_t* in, size_t len, uint8_t* out) {
    if (keyLen < 4 || keyLen > 56) {
        LOG_E(Log::CRYPTO, "blowfish bad key length: %u", (unsigned)keyLen);
        return false;
    }
    if (len % kBlowfishBlock != 0) {
        LOG_W(Log::CRYPTO, "blowfish length not multiple of 8: %u", (unsigned)len);
        return false;
    }

    uint8_t ivBuf[kBlowfishBlock];
    memcpy(ivBuf, iv, kBlowfishBlock);  // crypt_cbc mutates iv; keep caller's intact

    tela_blowfish_context ctx;
    tela_blowfish_init(&ctx);
    int ret = tela_blowfish_setkey(&ctx, key, (unsigned)(keyLen * 8));
    if (ret == 0) {
        ret = tela_blowfish_crypt_cbc(
            &ctx,
            encrypt ? BF_ENCRYPT : BF_DECRYPT,
            len, ivBuf, in, out);
    }
    tela_blowfish_free(&ctx);

    if (ret != 0) {
        LOG_E(Log::CRYPTO, "blowfish cbc failed: -0x%04x", -ret);
        return false;
    }
    return true;
}

} // namespace Crypto
