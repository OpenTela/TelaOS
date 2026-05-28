#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * lua_crypto.h — exposes the Crypto engine to Lua as the `crypto.*` namespace.
 *
 *   crypto.random(n)                  -> bytes
 *   crypto.sha256(data)               -> bytes (32)
 *   crypto.md5(data)                  -> bytes (16)   [legacy hash]
 *   crypto.hmac256(key, data)         -> bytes (32)
 *   crypto.pbkdf2(pw, salt, iters, n) -> bytes (n)
 *   crypto.encrypt(key, plain[, aad]) -> blob               (AES-GCM, iv||ct||tag)
 *   crypto.decrypt(key, blob[, aad])  -> plain | nil, err
 *   crypto.blowfish_encrypt(key, iv, data) -> bytes  [legacy CBC; len % 8 == 0]
 *   crypto.blowfish_decrypt(key, iv, data) -> bytes  [iv = 8 bytes, key 4..56]
 *   crypto.hex(bytes)                 -> hex string
 *   crypto.unhex(hex)                 -> bytes
 *
 * All byte values are plain Lua strings (binary-safe).
 */
namespace LuaCrypto {

void registerAll(lua_State* L);

} // namespace LuaCrypto
