#pragma once
// Mock esp_ota_ops for host tests. Captures the written image so tests can
// assert exactly what would be flashed. Supports failure injection.
#include <cstdint>
#include <cstring>
#include <cstdlib>

typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
#ifndef ESP_FAIL
#define ESP_FAIL -1
#endif
#ifndef OTA_SIZE_UNKNOWN
#define OTA_SIZE_UNKNOWN 0xffffffffU
#endif

inline const char* esp_err_to_name(esp_err_t e) { return e == ESP_OK ? "ESP_OK" : "ESP_FAIL"; }

typedef struct {
    uint32_t    address;
    const char* label;
} esp_partition_t;

typedef uintptr_t esp_ota_handle_t;

namespace OtaMock {
    inline esp_partition_t part      = { 0x310000, "app1" };
    inline uint8_t*        written   = nullptr;  // captured image
    inline uint32_t        writtenLen = 0;
    inline uint32_t        beginSize = 0;        // size passed to esp_ota_begin
    inline bool            ended     = false;
    inline bool            aborted   = false;
    inline bool            bootSet   = false;
    // failure injection (set before running OtaReceive::process)
    inline esp_err_t failBegin = ESP_OK;
    inline esp_err_t failWrite = ESP_OK;
    inline esp_err_t failEnd   = ESP_OK;
    inline esp_err_t failBoot  = ESP_OK;

    inline void reset() {
        if (written) { free(written); written = nullptr; }
        writtenLen = beginSize = 0;
        ended = aborted = bootSet = false;
        failBegin = failWrite = failEnd = failBoot = ESP_OK;
    }
}

inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) {
    return &OtaMock::part;
}

inline esp_err_t esp_ota_begin(const esp_partition_t*, size_t size, esp_ota_handle_t* out) {
    if (OtaMock::failBegin != ESP_OK) return OtaMock::failBegin;
    OtaMock::beginSize = (uint32_t)size;
    if (OtaMock::written) free(OtaMock::written);
    OtaMock::written = (uint8_t*)malloc(size ? size : 1);
    OtaMock::writtenLen = 0;
    if (out) *out = 1;
    return ESP_OK;
}

inline esp_err_t esp_ota_write(esp_ota_handle_t, const void* data, size_t size) {
    if (OtaMock::failWrite != ESP_OK) return OtaMock::failWrite;
    memcpy(OtaMock::written + OtaMock::writtenLen, data, size);
    OtaMock::writtenLen += (uint32_t)size;
    return ESP_OK;
}

inline esp_err_t esp_ota_end(esp_ota_handle_t) {
    if (OtaMock::failEnd != ESP_OK) return OtaMock::failEnd;
    OtaMock::ended = true;
    return ESP_OK;
}

inline esp_err_t esp_ota_abort(esp_ota_handle_t) {
    OtaMock::aborted = true;
    return ESP_OK;
}

inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t*) {
    if (OtaMock::failBoot != ESP_OK) return OtaMock::failBoot;
    OtaMock::bootSet = true;
    return ESP_OK;
}
