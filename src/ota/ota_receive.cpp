#include "ota/ota_receive.h"
#include "ble/ble_bridge.h"
#include "ble/bin_stream.h"
#include "crypto/crypto_engine.h"
#include "utils/log_config.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <lz4.h>
#include <cstring>
#include <strings.h>   // strcasecmp

static const char* TAG = "OtaReceive";

namespace OtaReceive {

// Max image = OTA partition size. app0/app1 are 0x300000 (3 MiB) each in partitions.csv.
static const uint32_t MAX_FW_SIZE = 0x300000;

static BinStream  s_stream;                 // shared BIN transport (receives compressed bytes)
static uint8_t*   s_comp     = nullptr;     // compressed image (BinStream sink), null if uncompressed
static uint8_t*   s_image    = nullptr;     // raw image written to flash
static uint32_t   s_rawSize  = 0;           // size of the raw image (== flash bytes)
static uint32_t   s_compSize = 0;           // size of compressed stream, 0 == no compression
static char       s_sha256Hex[65] = {};
static bool       s_haveHash = false;
static bool       s_readyToFlash = false;

// --- result reporting (text channel, like BinReceive) -----------------------

static void sendResult(bool ok, const char* msg) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"cmd\":\"ota\",\"status\":\"%s\",\"msg\":\"%s\"}",
             ok ? "ok" : "error", msg);
    BLEBridge::send(P::String(buf));
}

static void cleanup() {
    if (s_comp)  { heap_caps_free(s_comp);  s_comp = nullptr; }
    if (s_image) { heap_caps_free(s_image); s_image = nullptr; }
    s_stream.reset();
    s_readyToFlash = false;
}

// --- decompress (block format, matches the project's screenshot LZ4) ---------

/// Fills s_image with the raw firmware. Returns false on size/decode mismatch.
static bool decompressImage() {
    if (s_compSize == 0) {
        // Uncompressed: BinStream wrote straight into s_image.
        return true;
    }

    int out = LZ4_decompress_safe((const char*)s_comp, (char*)s_image,
                                  (int)s_compSize, (int)s_rawSize);
    if (out < 0) {
        LOG_E(Log::BLE, "OTA: LZ4 decode failed (%d)", out);
        return false;
    }
    if ((uint32_t)out != s_rawSize) {
        LOG_E(Log::BLE, "OTA: decoded size %d != raw %u", out, s_rawSize);
        return false;
    }

    // Compressed buffer no longer needed; free early to ease PSRAM pressure.
    heap_caps_free(s_comp);
    s_comp = nullptr;

    LOG_I(Log::BLE, "OTA: LZ4 decoded %u -> %u bytes", s_compSize, s_rawSize);
    return true;
}

// --- integrity check (SHA-256 of the RAW image) -----------------------------

static bool verifyHash() {
    if (!s_haveHash) {
        LOG_W(Log::BLE, "OTA: no hash provided, skipping integrity check");
        return true;
    }

    uint8_t digest[Crypto::kSha256Len];
    if (!Crypto::sha256(s_image, s_rawSize, digest)) {
        LOG_E(Log::BLE, "OTA: sha256 computation failed");
        return false;
    }

    static const char* HEX = "0123456789abcdef";
    char hex[65];
    for (int i = 0; i < 32; i++) {
        hex[i * 2]     = HEX[digest[i] >> 4];
        hex[i * 2 + 1] = HEX[digest[i] & 0x0F];
    }
    hex[64] = '\0';

    if (strcasecmp(hex, s_sha256Hex) != 0) {
        LOG_E(Log::BLE, "OTA: hash mismatch");
        LOG_E(Log::BLE, "  got: %s", hex);
        LOG_E(Log::BLE, "  exp: %s", s_sha256Hex);
        return false;
    }

    LOG_I(Log::BLE, "OTA: sha256 verified");
    return true;
}

// --- flash + boot -----------------------------------------------------------

static bool flashAndBoot() {
    const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
    if (!part) {
        LOG_E(Log::APP, "OTA: no inactive OTA partition found");
        return false;
    }

    LOG_I(Log::APP, "OTA: writing %u bytes to '%s' @0x%lx",
          s_rawSize, part->label, (unsigned long)part->address);

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, s_rawSize, &handle);   // erases s_rawSize bytes
    if (err != ESP_OK) {
        LOG_E(Log::APP, "OTA: esp_ota_begin failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_write(handle, s_image, s_rawSize);           // whole image at once
    if (err != ESP_OK) {
        LOG_E(Log::APP, "OTA: esp_ota_write failed: %s", esp_err_to_name(err));
        esp_ota_abort(handle);
        return false;
    }

    err = esp_ota_end(handle);   // validates image magic / header (and signature if Secure Boot)
    if (err != ESP_OK) {
        LOG_E(Log::APP, "OTA: esp_ota_end/validate failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        LOG_E(Log::APP, "OTA: esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return false;
    }

    LOG_I(Log::APP, "OTA: success - next boot from '%s'", part->label);
    return true;
}

// --- public API -------------------------------------------------------------

bool start(uint32_t rawSize, uint32_t compSize, const char* sha256Hex) {
    if (s_stream.isActive() || s_comp || s_image) {
        LOG_W(Log::BLE, "OTA already active, cancelling previous");
        cleanup();
    }

    if (rawSize == 0 || rawSize > MAX_FW_SIZE) {
        LOG_E(Log::BLE, "OTA: invalid raw size %u (max %u)", rawSize, MAX_FW_SIZE);
        return false;
    }
    // compSize must not exceed the LZ4 worst-case bound for rawSize.
    if (compSize > (uint32_t)LZ4_compressBound((int)rawSize)) {
        LOG_E(Log::BLE, "OTA: comp size %u implausible for raw %u", compSize, rawSize);
        return false;
    }

    s_rawSize  = rawSize;
    s_compSize = compSize;

    // Always need the raw image buffer (flash source).
    s_image = (uint8_t*)heap_caps_malloc(rawSize, MALLOC_CAP_SPIRAM);
    if (!s_image) {
        LOG_E(Log::BLE, "OTA: failed to allocate raw buffer %u bytes", rawSize);
        return false;
    }

    // Compressed transfers land in s_comp; uncompressed land straight in s_image.
    uint32_t streamSize;
    uint8_t* sink;
    if (compSize > 0) {
        s_comp = (uint8_t*)heap_caps_malloc(compSize, MALLOC_CAP_SPIRAM);
        if (!s_comp) {
            LOG_E(Log::BLE, "OTA: failed to allocate comp buffer %u bytes", compSize);
            heap_caps_free(s_image);
            s_image = nullptr;
            return false;
        }
        streamSize = compSize;
        sink = s_comp;
    } else {
        streamSize = rawSize;
        sink = s_image;
    }

    s_haveHash = false;
    s_sha256Hex[0] = '\0';
    if (sha256Hex && strlen(sha256Hex) == 64) {
        strncpy(s_sha256Hex, sha256Hex, sizeof(s_sha256Hex) - 1);
        s_haveHash = true;
    } else if (sha256Hex && sha256Hex[0]) {
        LOG_W(Log::BLE, "OTA: malformed hash (length != 64), ignoring");
    }

    s_readyToFlash = false;

    // BinStream memcpys each chunk into the sink; framing errors drop everything.
    s_stream.begin(
        streamSize,
        [sink](const uint8_t* data, uint32_t len) {
            memcpy(sink + s_stream.received(), data, len);
        },
        [](BinStream::Error) {
            sendResult(false, "chunk framing error");
            cleanup();
        });

    LOG_I(Log::BLE, "OTA start: raw=%u comp=%u%s", rawSize, compSize,
          s_haveHash ? ", sha256 armed" : ", NO hash");
    return true;
}

void onChunk(const uint8_t* data, uint32_t len) {
    if (!s_stream.isActive()) return;

    s_stream.onChunk(data, len);

    if (s_stream.isComplete()) {
        LOG_I(Log::BLE, "OTA: received %u bytes - flash deferred to main loop", s_stream.received());
        s_readyToFlash = true;
    }
}

bool isInProgress() {
    return s_stream.isActive();
}

void process() {
    if (!s_readyToFlash) return;
    s_readyToFlash = false;

    if (!decompressImage()) {
        sendResult(false, "decompress failed");
        cleanup();
        return;
    }

    if (!verifyHash()) {
        sendResult(false, "hash mismatch");
        cleanup();
        return;
    }

    bool ok = flashAndBoot();
    sendResult(ok, ok ? "flashed, rebooting" : "flash failed");

    cleanup();   // free PSRAM regardless; on success we reboot anyway

    if (ok) {
        delay(300);       // let the TX notify flush before we drop the link
        esp_restart();
    }
}

void cancel() {
    if (s_stream.isActive() || s_comp || s_image) {
        LOG_W(Log::BLE, "OTA cancelled");
        cleanup();
    }
}

} // namespace OtaReceive
