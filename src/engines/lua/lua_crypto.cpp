#include "engines/lua/lua_crypto.h"
#include "crypto/crypto_engine.h"
#include "utils/log_config.h"

#include <string>

namespace LuaCrypto {

static const char* TAG = "LuaCrypto";

// crypto.random(n) -> bytes
static int lua_random(lua_State* L) {
    lua_Integer n = luaL_checkinteger(L, 1);
    luaL_argcheck(L, n >= 0 && n <= 4096, 1, "size out of range (0..4096)");
    std::string out;
    if (!Crypto::random((size_t)n, out)) {
        return luaL_error(L, "crypto.random: RNG failure");
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// crypto.sha256(data) -> bytes(32)
static int lua_sha256(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    uint8_t digest[Crypto::kSha256Len];
    if (!Crypto::sha256((const uint8_t*)data, len, digest)) {
        return luaL_error(L, "crypto.sha256 failed");
    }
    lua_pushlstring(L, (const char*)digest, sizeof(digest));
    return 1;
}

// crypto.hmac256(key, data) -> bytes(32)
static int lua_hmac256(lua_State* L) {
    size_t keyLen = 0, dataLen = 0;
    const char* key  = luaL_checklstring(L, 1, &keyLen);
    const char* data = luaL_checklstring(L, 2, &dataLen);
    uint8_t mac[Crypto::kSha256Len];
    if (!Crypto::hmacSha256((const uint8_t*)key, keyLen,
                            (const uint8_t*)data, dataLen, mac)) {
        return luaL_error(L, "crypto.hmac256 failed");
    }
    lua_pushlstring(L, (const char*)mac, sizeof(mac));
    return 1;
}

// crypto.pbkdf2(password, salt, iterations, dkLen) -> bytes(dkLen)
static int lua_pbkdf2(lua_State* L) {
    size_t pwLen = 0, saltLen = 0;
    const char* pw   = luaL_checklstring(L, 1, &pwLen);
    const char* salt = luaL_checklstring(L, 2, &saltLen);
    lua_Integer iters = luaL_checkinteger(L, 3);
    lua_Integer dkLen = luaL_checkinteger(L, 4);
    luaL_argcheck(L, iters > 0, 3, "iterations must be > 0");
    luaL_argcheck(L, dkLen > 0 && dkLen <= 1024, 4, "dkLen out of range (1..1024)");

    std::string out;
    if (!Crypto::pbkdf2Sha256((const uint8_t*)pw, pwLen,
                              (const uint8_t*)salt, saltLen,
                              (uint32_t)iters, (size_t)dkLen, out)) {
        return luaL_error(L, "crypto.pbkdf2 failed");
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// crypto.encrypt(key, plaintext [, aad]) -> blob
static int lua_encrypt(lua_State* L) {
    size_t keyLen = 0, ptLen = 0, aadLen = 0;
    const char* key = luaL_checklstring(L, 1, &keyLen);
    const char* pt  = luaL_checklstring(L, 2, &ptLen);
    const char* aad = luaL_optlstring(L, 3, nullptr, &aadLen);

    std::string out;
    if (!Crypto::aesGcmEncrypt((const uint8_t*)key, keyLen,
                               (const uint8_t*)pt, ptLen,
                               (const uint8_t*)aad, aadLen, out)) {
        return luaL_error(L, "crypto.encrypt failed (key must be 16/24/32 bytes)");
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// crypto.decrypt(key, blob [, aad]) -> plaintext | nil, err
static int lua_decrypt(lua_State* L) {
    size_t keyLen = 0, blobLen = 0, aadLen = 0;
    const char* key  = luaL_checklstring(L, 1, &keyLen);
    const char* blob = luaL_checklstring(L, 2, &blobLen);
    const char* aad  = luaL_optlstring(L, 3, nullptr, &aadLen);

    std::string out;
    if (!Crypto::aesGcmDecrypt((const uint8_t*)key, keyLen,
                               (const uint8_t*)blob, blobLen,
                               (const uint8_t*)aad, aadLen, out)) {
        lua_pushnil(L);
        lua_pushstring(L, "auth failed (wrong key or tampered data)");
        return 2;
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// crypto.hex(bytes) -> hex string
static int lua_hex(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    static const char* H = "0123456789abcdef";
    std::string s;
    s.resize(len * 2);
    for (size_t i = 0; i < len; ++i) {
        unsigned char b = (unsigned char)data[i];
        s[2 * i]     = H[b >> 4];
        s[2 * i + 1] = H[b & 0x0f];
    }
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

// crypto.unhex(hex) -> bytes
static int lua_unhex(lua_State* L) {
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    luaL_argcheck(L, (len % 2) == 0, 1, "hex length must be even");
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    out.resize(len / 2);
    for (size_t i = 0; i < len / 2; ++i) {
        int hi = nib(s[2 * i]), lo = nib(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return luaL_error(L, "crypto.unhex: invalid hex");
        out[i] = (char)((hi << 4) | lo);
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// crypto.md5(data) -> bytes(16)   [legacy hash, e.g. legacy file formats]
static int lua_md5(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 1, &len);
    uint8_t digest[Crypto::kMd5Len];
    if (!Crypto::md5((const uint8_t*)data, len, digest)) {
        return luaL_error(L, "crypto.md5 failed");
    }
    lua_pushlstring(L, (const char*)digest, sizeof(digest));
    return 1;
}

// crypto.blowfish_encrypt(key, iv, data) -> bytes
// crypto.blowfish_decrypt(key, iv, data) -> bytes
// key 4..56 bytes, iv exactly 8 bytes, data length multiple of 8.
static int blowfish_impl(lua_State* L, bool encrypt) {
    size_t keyLen = 0, ivLen = 0, dataLen = 0;
    const char* key  = luaL_checklstring(L, 1, &keyLen);
    const char* iv   = luaL_checklstring(L, 2, &ivLen);
    const char* data = luaL_checklstring(L, 3, &dataLen);
    luaL_argcheck(L, ivLen == Crypto::kBlowfishBlock, 2, "iv must be 8 bytes");
    luaL_argcheck(L, dataLen % Crypto::kBlowfishBlock == 0, 3,
                  "data length must be a multiple of 8");
    std::string out;
    out.resize(dataLen);
    if (!Crypto::blowfishCbc(encrypt, (const uint8_t*)key, keyLen,
                             (const uint8_t*)iv, (const uint8_t*)data, dataLen,
                             dataLen ? (uint8_t*)&out[0] : nullptr)) {
        return luaL_error(L, "crypto.blowfish failed (key must be 4..56 bytes)");
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}
static int lua_blowfish_encrypt(lua_State* L) { return blowfish_impl(L, true); }
static int lua_blowfish_decrypt(lua_State* L) { return blowfish_impl(L, false); }

static const luaL_Reg crypto_lib[] = {
    {"random",  lua_random},
    {"sha256",  lua_sha256},
    {"md5",     lua_md5},
    {"hmac256", lua_hmac256},
    {"pbkdf2",  lua_pbkdf2},
    {"encrypt", lua_encrypt},
    {"decrypt", lua_decrypt},
    {"blowfish_encrypt", lua_blowfish_encrypt},
    {"blowfish_decrypt", lua_blowfish_decrypt},
    {"hex",     lua_hex},
    {"unhex",   lua_unhex},
    {nullptr, nullptr}
};

void registerAll(lua_State* L) {
    Crypto::init();  // seed RNG up front (best-effort; safe to retry later)
    luaL_newlib(L, crypto_lib);
    lua_setglobal(L, "crypto");
    LOG_I(Log::CRYPTO, "Registered: crypto.*");
}

} // namespace LuaCrypto
