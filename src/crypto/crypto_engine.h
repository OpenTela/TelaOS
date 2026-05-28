#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/**
 * crypto_engine.h — TelaOS crypto core.
 *
 * Thin facade over mbedTLS (2.28, shipped with the ESP-IDF/Arduino stack).
 * Provides modern primitives only; all randomness comes from a single
 * CSPRNG (CTR_DRBG) seeded from the platform entropy source (ESP32 HW RNG).
 *
 * Conventions:
 *   - All buffers are raw bytes. Hex/base64 framing belongs in higher layers.
 *   - Digest/derive outputs are raw bytes, never hex.
 *   - AEAD owns nonce generation: callers never supply an IV (footgun removed).
 */
namespace Crypto {

// AES-GCM framing produced by aesGcmEncrypt / consumed by aesGcmDecrypt.
inline constexpr size_t kGcmIvLen  = 12;   // 96-bit nonce (GCM recommended)
inline constexpr size_t kGcmTagLen = 16;   // 128-bit auth tag
inline constexpr size_t kSha256Len = 32;

// Lazily seeds the CSPRNG. Idempotent. Safe to call repeatedly.
// Returns false if the entropy source could not seed the DRBG.
// NOTE: on ESP32 the HW RNG only yields true entropy once the RF subsystem
// (Wi-Fi/BT) is up — call init() after BLE has started.
bool init();

// Fills `out` with `n` cryptographically secure random bytes.
bool random(size_t n, std::string& out);

// SHA-256. `out` receives kSha256Len raw bytes. Returns false on internal error.
bool sha256(const uint8_t* data, size_t len, uint8_t out[kSha256Len]);

inline constexpr size_t kMd5Len = 16;

// MD5. `out` receives kMd5Len raw bytes. Legacy primitive — provided for
// interop with legacy formats, NOT for new security uses.
bool md5(const uint8_t* data, size_t len, uint8_t out[kMd5Len]);

inline constexpr size_t kBlowfishBlock = 8;

// Blowfish-CBC (standard, vendored). key 4..56 bytes; iv is kBlowfishBlock bytes
// and is consumed (not modified). `len` MUST be a multiple of kBlowfishBlock.
// `out` must hold `len` bytes. encrypt=true to encrypt, false to decrypt.
// Legacy 64-bit-block cipher — interop only (legacy/standard Blowfish files). New data → AES-GCM.
// Padding and file framing are the caller's responsibility (Lua codec layer).
bool blowfishCbc(bool encrypt,
                 const uint8_t* key, size_t keyLen,
                 const uint8_t iv[kBlowfishBlock],
                 const uint8_t* in, size_t len, uint8_t* out);

// HMAC-SHA256. `out` receives kSha256Len raw bytes.
bool hmacSha256(const uint8_t* key, size_t keyLen,
                const uint8_t* data, size_t dataLen,
                uint8_t out[kSha256Len]);

// PBKDF2-HMAC-SHA256. Derives `dkLen` bytes into `out`.
// This is the password-stretching primitive — pick `iterations` as high as the
// unlock latency tolerates (e.g. 100k+). Argon2 is impractical here (no spare RAM).
bool pbkdf2Sha256(const uint8_t* password, size_t pwLen,
                  const uint8_t* salt, size_t saltLen,
                  uint32_t iterations, size_t dkLen,
                  std::string& out);

// AES-GCM authenticated encryption (AES-128/192/256 by key length 16/24/32).
// A fresh random 12-byte IV is generated internally.
// Output layout: IV(12) || ciphertext(plainLen) || tag(16).
// `aad` may be null/0. Returns false on bad key size / RNG / internal error.
bool aesGcmEncrypt(const uint8_t* key, size_t keyLen,
                   const uint8_t* plain, size_t plainLen,
                   const uint8_t* aad, size_t aadLen,
                   std::string& out);

// AES-GCM decrypt + verify. `blob` must be IV(12) || ciphertext || tag(16).
// Returns false if the tag does not verify (tampered / wrong key) or the
// input is malformed. On success `out` holds the recovered plaintext.
bool aesGcmDecrypt(const uint8_t* key, size_t keyLen,
                   const uint8_t* blob, size_t blobLen,
                   const uint8_t* aad, size_t aadLen,
                   std::string& out);

} // namespace Crypto
