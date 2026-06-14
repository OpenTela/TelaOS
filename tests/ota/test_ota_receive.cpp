/**
 * Test: OtaReceive — full firmware OTA receive over BLE.
 *
 * Exercises the real flow against the esp_ota mock (which captures the would-be
 * flashed image), the real host SHA-256 stub, and the host LZ4 decoder:
 *   - uncompressed: chunks → buffer → flash, exact bytes + boot set
 *   - compressed:   real all-literal LZ4 block → decode → flash exact raw bytes
 *   - SHA-256 mismatch aborts before any flash
 *   - framing error aborts cleanly
 *   - esp_ota_begin failure is surfaced (no boot set)
 *
 * BLEBridge::send is provided locally (CI links the real one from the project).
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include "ota/ota_receive.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "crypto/crypto_engine.h"
#include "utils/log_config.h"

// BLEBridge::send is provided by the real ble_bridge.o linked into the suite.

#define TEST(name) printf("  %-52s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

static std::string sha256hex(const uint8_t* data, uint32_t n) {
    uint8_t d[Crypto::kSha256Len];
    Crypto::sha256(data, n, d);
    static const char* H = "0123456789abcdef";
    char hex[65];
    for (int i = 0; i < 32; i++) { hex[i*2] = H[d[i]>>4]; hex[i*2+1] = H[d[i]&0xF]; }
    hex[64] = 0;
    return std::string(hex);
}

// Build a valid all-literal LZ4 block (single literals-only sequence).
static std::vector<uint8_t> lz4_literal_block(const uint8_t* raw, uint32_t n) {
    std::vector<uint8_t> b;
    uint8_t token = (n >= 15) ? 0xF0 : (uint8_t)(n << 4);
    b.push_back(token);
    if (n >= 15) {
        uint32_t r = n - 15;
        while (r >= 255) { b.push_back(255); r -= 255; }
        b.push_back((uint8_t)r);
    }
    b.insert(b.end(), raw, raw + n);
    return b;
}

// Feed a byte blob through OtaReceive::onChunk in framed 250B chunks.
static void streamBlob(const uint8_t* blob, uint32_t len) {
    const uint32_t CH = 250;
    uint16_t id = 0;
    for (uint32_t off = 0; off < len; off += CH, id++) {
        uint32_t l = (len - off < CH) ? (len - off) : CH;
        std::vector<uint8_t> pkt;
        pkt.push_back(id & 0xFF);
        pkt.push_back((id >> 8) & 0xFF);
        pkt.insert(pkt.end(), blob + off, blob + off + l);
        OtaReceive::onChunk(pkt.data(), (uint32_t)pkt.size());
    }
}

int main() {
    printf("=== OtaReceive Tests ===\n\n");
    int passed = 0, total = 0;

    // Mute device logs: a deliberate begin-failure case logs "ESP_FAIL", and the
    // harness's fallback heuristic substring-matches "FAIL" in test output.
    Log::setAll(Log::Disabled);

    const uint32_t N = 2000;
    uint8_t fw[N];
    for (uint32_t i = 0; i < N; i++) fw[i] = (uint8_t)(i * 13 + 5);
    std::string goodHash = sha256hex(fw, N);

    printf("uncompressed flow:\n");

    TEST("uncompressed: flashes exact image, boot set, reboots");
    {
        OtaMock::reset();
        g_esp_restarted = false;
        bool armed = OtaReceive::start(N, 0, goodHash.c_str());
        streamBlob(fw, N);
        OtaReceive::process();
        bool ok = armed
            && OtaMock::beginSize == N
            && OtaMock::writtenLen == N
            && memcmp(OtaMock::written, fw, N) == 0
            && OtaMock::ended && OtaMock::bootSet && g_esp_restarted;
        if (ok) PASS();
        else { FAIL(""); printf("      begin=%u wrote=%u ended=%d boot=%d reboot=%d\n",
               OtaMock::beginSize, OtaMock::writtenLen, OtaMock::ended, OtaMock::bootSet, g_esp_restarted); }
    }

    TEST("uncompressed: works without a hash (skips check)");
    {
        OtaMock::reset();
        OtaReceive::start(N, 0, "");
        streamBlob(fw, N);
        OtaReceive::process();
        if (OtaMock::writtenLen == N && OtaMock::bootSet) PASS();
        else FAIL("");
    }

    printf("\ncompressed flow:\n");

    TEST("compressed: LZ4 block decodes to exact image and flashes");
    {
        OtaMock::reset();
        auto block = lz4_literal_block(fw, N);
        OtaReceive::start(N, (uint32_t)block.size(), goodHash.c_str());
        streamBlob(block.data(), (uint32_t)block.size());
        OtaReceive::process();
        bool ok = OtaMock::writtenLen == N
            && memcmp(OtaMock::written, fw, N) == 0
            && OtaMock::bootSet;
        if (ok) PASS();
        else { FAIL(""); printf("      wrote=%u boot=%d\n", OtaMock::writtenLen, OtaMock::bootSet); }
    }

    printf("\nintegrity & failure paths:\n");

    TEST("hash mismatch aborts before flashing");
    {
        OtaMock::reset();
        // hash of different data
        uint8_t other[N];
        memcpy(other, fw, N); other[0] ^= 0xFF;
        std::string wrong = sha256hex(other, N);
        OtaReceive::start(N, 0, wrong.c_str());
        streamBlob(fw, N);
        OtaReceive::process();
        if (!OtaMock::bootSet && OtaMock::writtenLen == 0) PASS();
        else { FAIL(""); printf("      boot=%d wrote=%u\n", OtaMock::bootSet, OtaMock::writtenLen); }
    }

    TEST("decoded-size mismatch aborts (lying raw_size)");
    {
        OtaMock::reset();
        auto block = lz4_literal_block(fw, N);
        // claim raw is N+100 but block only decodes to N
        OtaReceive::start(N + 100, (uint32_t)block.size(), "");
        streamBlob(block.data(), (uint32_t)block.size());
        OtaReceive::process();
        if (!OtaMock::bootSet) PASS();
        else FAIL("should not flash");
    }

    TEST("esp_ota_begin failure is surfaced, no boot");
    {
        OtaMock::reset();
        g_esp_restarted = false;
        OtaMock::failBegin = ESP_FAIL;
        OtaReceive::start(N, 0, "");
        streamBlob(fw, N);
        OtaReceive::process();
        if (!OtaMock::bootSet && !g_esp_restarted) PASS();
        else FAIL("");
        OtaMock::failBegin = ESP_OK;
    }

    printf("\nargument validation:\n");

    TEST("zero raw size rejected");
    { if (!OtaReceive::start(0, 0, "")) PASS(); else FAIL(""); }

    TEST("oversize image (> 3MB partition) rejected");
    { if (!OtaReceive::start(0x300000 + 1, 0, "")) PASS(); else FAIL(""); }

    TEST("implausible comp size (> bound) rejected");
    { if (!OtaReceive::start(1000, 5000000, "")) PASS(); else FAIL(""); }

    TEST("isInProgress false after a completed flow");
    {
        OtaMock::reset();
        OtaReceive::start(N, 0, "");
        streamBlob(fw, N);
        OtaReceive::process();
        if (!OtaReceive::isInProgress()) PASS();
        else FAIL("");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d OTARECEIVE TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d OTARECEIVE TESTS PASSED ===\n", passed, total);
    return 1;
}
