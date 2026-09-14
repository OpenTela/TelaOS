/**
 * Test: Crypto Engine
 * Known-answer vectors (RFC 4231, PBKDF2-HMAC-SHA256, NIST GCM) +
 * AEAD roundtrip / tamper / wrong-key / AAD-binding + RNG sanity.
 *
 * Links against the project's crypto_engine.o (build/mock) and libmbedcrypto.
 */
#include <cstdio>
#include <cstring>
#include <string>
#include "crypto/crypto_engine.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("\u2713\n"); passed++; } while(0)
#define FAIL(msg) printf("\u2717 %s\n", msg)

static std::string toHex(const unsigned char* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string o; o.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { o += H[p[i] >> 4]; o += H[p[i] & 0x0f]; }
    return o;
}
static std::string toHex(const std::string& s) {
    return toHex(reinterpret_cast<const unsigned char*>(s.data()), s.size());
}
static std::string unHex(const char* s) {
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    std::string o;
    for (size_t i = 0; s[i] && s[i + 1]; i += 2)
        o += static_cast<char>((v(s[i]) << 4) | v(s[i + 1]));
    return o;
}

int main() {
    printf("=== Crypto Engine Tests ===\n\n");
    int passed = 0, total = 0;

    // === Hash ===
    printf("SHA-256:\n");
    TEST("SHA256(\"abc\") matches FIPS-180 vector") {
        unsigned char d[Crypto::kSha256Len];
        Crypto::sha256(reinterpret_cast<const unsigned char*>("abc"), 3, d);
        if (toHex(d, sizeof(d)) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
            PASS(); else FAIL("digest mismatch");
    }
    TEST("SHA256(\"\") matches vector") {
        unsigned char d[Crypto::kSha256Len];
        Crypto::sha256(reinterpret_cast<const unsigned char*>(""), 0, d);
        if (toHex(d, sizeof(d)) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
            PASS(); else FAIL("digest mismatch");
    }

    // === HMAC (RFC 4231) ===
    printf("\nHMAC-SHA256 (RFC 4231):\n");
    TEST("case 2: key=\"Jefe\"") {
        unsigned char m[Crypto::kSha256Len];
        const char* data = "what do ya want for nothing?";
        Crypto::hmacSha256(reinterpret_cast<const unsigned char*>("Jefe"), 4,
                           reinterpret_cast<const unsigned char*>(data),
                           strlen(data), m);
        if (toHex(m, sizeof(m)) ==
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")
            PASS(); else FAIL("mac mismatch");
    }

    // === PBKDF2-HMAC-SHA256 ===
    printf("\nPBKDF2-HMAC-SHA256:\n");
    TEST("(passwd, salt, c=1, dkLen=64)") {
        std::string out;
        Crypto::pbkdf2Sha256(reinterpret_cast<const unsigned char*>("passwd"), 6,
                             reinterpret_cast<const unsigned char*>("salt"), 4,
                             1, 64, out);
        if (toHex(out).substr(0, 32) == "55ac046e56e3089fec1691c22544b605")
            PASS(); else FAIL("derived key mismatch");
    }
    TEST("(password, salt, c=2, dkLen=32)") {
        std::string out;
        Crypto::pbkdf2Sha256(reinterpret_cast<const unsigned char*>("password"), 8,
                             reinterpret_cast<const unsigned char*>("salt"), 4,
                             2, 32, out);
        if (toHex(out) ==
            "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43")
            PASS(); else FAIL("derived key mismatch");
    }

    // === AES-GCM ===
    printf("\nAES-GCM:\n");
    TEST("AES-128-GCM NIST decrypt KAT (all-zero)") {
        std::string key(16, '\0'), iv(12, '\0');
        std::string ct  = unHex("0388dace60b6a392f328c2b971b2fe78");
        std::string tag = unHex("ab6e47d42cec13bdf53a67b21257bddf");
        std::string blob = iv + ct + tag, pt;
        bool ok = Crypto::aesGcmDecrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 16,
            reinterpret_cast<const unsigned char*>(blob.data()), blob.size(),
            nullptr, 0, pt);
        if (ok && pt == std::string(16, '\0')) PASS(); else FAIL("KAT failed");
    }
    TEST("AES-256-GCM roundtrip") {
        std::string key(32, 'k'), pt = "hello tela secret note", blob, out;
        bool e = Crypto::aesGcmEncrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 32,
            reinterpret_cast<const unsigned char*>(pt.data()), pt.size(),
            nullptr, 0, blob);
        bool d = Crypto::aesGcmDecrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 32,
            reinterpret_cast<const unsigned char*>(blob.data()), blob.size(),
            nullptr, 0, out);
        if (e && d && out == pt) PASS(); else FAIL("roundtrip failed");
    }
    TEST("blob layout = IV(12) + ct + tag(16)") {
        std::string key(32, 'k'), pt = "0123456789", blob;
        Crypto::aesGcmEncrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 32,
            reinterpret_cast<const unsigned char*>(pt.data()), pt.size(),
            nullptr, 0, blob);
        if (blob.size() == 12 + pt.size() + 16) PASS(); else FAIL("bad length");
    }
    TEST("two encryptions of same input differ (random IV)") {
        std::string key(32, 'k'), pt = "same", a, b;
        Crypto::aesGcmEncrypt(reinterpret_cast<const unsigned char*>(key.data()),
                              32, reinterpret_cast<const unsigned char*>(pt.data()),
                              pt.size(), nullptr, 0, a);
        Crypto::aesGcmEncrypt(reinterpret_cast<const unsigned char*>(key.data()),
                              32, reinterpret_cast<const unsigned char*>(pt.data()),
                              pt.size(), nullptr, 0, b);
        if (a != b) PASS(); else FAIL("IV reuse — ciphertexts identical");
    }
    TEST("rejects tampered ciphertext") {
        std::string key(32, 'k'), pt = "tamper me", blob, out;
        Crypto::aesGcmEncrypt(reinterpret_cast<const unsigned char*>(key.data()),
                              32, reinterpret_cast<const unsigned char*>(pt.data()),
                              pt.size(), nullptr, 0, blob);
        blob[15] ^= 0x01;
        bool d = Crypto::aesGcmDecrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 32,
            reinterpret_cast<const unsigned char*>(blob.data()), blob.size(),
            nullptr, 0, out);
        if (!d) PASS(); else FAIL("accepted tampered data");
    }
    TEST("rejects wrong key") {
        std::string key(32, 'k'), bad(32, 'x'), pt = "secret", blob, out;
        Crypto::aesGcmEncrypt(reinterpret_cast<const unsigned char*>(key.data()),
                              32, reinterpret_cast<const unsigned char*>(pt.data()),
                              pt.size(), nullptr, 0, blob);
        bool d = Crypto::aesGcmDecrypt(
            reinterpret_cast<const unsigned char*>(bad.data()), 32,
            reinterpret_cast<const unsigned char*>(blob.data()), blob.size(),
            nullptr, 0, out);
        if (!d) PASS(); else FAIL("accepted wrong key");
    }
    TEST("rejects wrong AAD") {
        std::string key(32, 'k'), pt = "p", blob, out;
        Crypto::aesGcmEncrypt(reinterpret_cast<const unsigned char*>(key.data()),
                              32, reinterpret_cast<const unsigned char*>(pt.data()),
                              pt.size(),
                              reinterpret_cast<const unsigned char*>("hdr"), 3, blob);
        bool d = Crypto::aesGcmDecrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 32,
            reinterpret_cast<const unsigned char*>(blob.data()), blob.size(),
            reinterpret_cast<const unsigned char*>("HDR"), 3, out);
        if (!d) PASS(); else FAIL("AAD not authenticated");
    }
    TEST("rejects bad key length (e.g. 20 bytes)") {
        std::string key(20, 'k'), pt = "x", blob;
        bool e = Crypto::aesGcmEncrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 20,
            reinterpret_cast<const unsigned char*>(pt.data()), pt.size(),
            nullptr, 0, blob);
        if (!e) PASS(); else FAIL("accepted invalid key size");
    }
    TEST("rejects truncated blob") {
        std::string key(32, 'k'), out;
        std::string tiny(10, '\0');  // < IV(12)+tag(16)
        bool d = Crypto::aesGcmDecrypt(
            reinterpret_cast<const unsigned char*>(key.data()), 32,
            reinterpret_cast<const unsigned char*>(tiny.data()), tiny.size(),
            nullptr, 0, out);
        if (!d) PASS(); else FAIL("accepted malformed blob");
    }

    // === MD5 (legacy, interop) ===
    printf("\nMD5:\n");
    TEST("MD5(\"abc\")") {
        unsigned char d[Crypto::kMd5Len];
        Crypto::md5(reinterpret_cast<const unsigned char*>("abc"), 3, d);
        if (toHex(d, sizeof(d)) == "900150983cd24fb0d6963f7d28e17f72")
            PASS(); else FAIL("digest mismatch");
    }
    TEST("MD5(\"\")") {
        unsigned char d[Crypto::kMd5Len];
        Crypto::md5(reinterpret_cast<const unsigned char*>(""), 0, d);
        if (toHex(d, sizeof(d)) == "d41d8cd98f00b204e9800998ecf8427e")
            PASS(); else FAIL("digest mismatch");
    }

    // === Blowfish-CBC (legacy, interop) ===
    printf("\nBlowfish-CBC:\n");
    TEST("KAT key=0^8 pt=0^8 (Eric Young)") {
        unsigned char key[8] = {0}, iv[8] = {0}, pt[8] = {0}, ct[8];
        bool ok = Crypto::blowfishCbc(true, key, 8, iv, pt, 8, ct);
        if (ok && toHex(ct, 8) == "4ef997456198dd78") PASS(); else FAIL("KAT mismatch");
    }
    TEST("KAT key=F^8 pt=F^8 (Eric Young)") {
        unsigned char key[8], iv[8] = {0}, pt[8], ct[8];
        memset(key, 0xFF, 8); memset(pt, 0xFF, 8);
        bool ok = Crypto::blowfishCbc(true, key, 8, iv, pt, 8, ct);
        if (ok && toHex(ct, 8) == "51866fd5b85ecb8a") PASS(); else FAIL("KAT mismatch");
    }
    TEST("CBC multi-block roundtrip (40B)") {
        unsigned char key[16], iv[8], pt[40], ct[40], back[40];
        for (int i = 0; i < 16; ++i) key[i] = i;
        for (int i = 0; i < 8;  ++i) iv[i] = 0xA0 + i;
        for (int i = 0; i < 40; ++i) pt[i] = i * 7;
        bool e = Crypto::blowfishCbc(true,  key, 16, iv, pt, 40, ct);
        bool d = Crypto::blowfishCbc(false, key, 16, iv, ct, 40, back);
        if (e && d && memcmp(pt, back, 40) == 0) PASS(); else FAIL("roundtrip failed");
    }
    TEST("rejects non-block-multiple length") {
        unsigned char key[8] = {0}, iv[8] = {0}, in[5] = {0}, out[5];
        bool ok = Crypto::blowfishCbc(true, key, 8, iv, in, 5, out);
        if (!ok) PASS(); else FAIL("accepted bad length");
    }

    // === RNG ===
    printf("\nRNG:\n");
    TEST("random(n) returns n bytes") {
        std::string r;
        if (Crypto::random(32, r) && r.size() == 32) PASS(); else FAIL("");
    }
    TEST("random differs across calls") {
        std::string a, b;
        Crypto::random(32, a); Crypto::random(32, b);
        if (a != b) PASS(); else FAIL("two draws equal");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d CRYPTO TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d CRYPTO TESTS PASSED ===\n", passed, total);
    return 1;
}
