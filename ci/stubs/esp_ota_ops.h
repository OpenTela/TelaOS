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
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif

// State of a firmware in otadata (ESP-IDF 4.4 esp_flash_partitions.h)
typedef enum {
    ESP_OTA_IMG_NEW            = 0x0U,
    ESP_OTA_IMG_PENDING_VERIFY = 0x1U,  // booted once, not confirmed: the next boot rolls it back
    ESP_OTA_IMG_VALID          = 0x2U,
    ESP_OTA_IMG_INVALID        = 0x3U,
    ESP_OTA_IMG_ABORTED        = 0x4U,
    ESP_OTA_IMG_UNDEFINED      = 0xFFFFFFFFU,
} esp_ota_img_states_t;

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
    // the running firmware and the rollback (esp_ota_get_running_partition and on)
    inline esp_partition_t       running      = { 0x10000, "app0" };
    inline esp_ota_img_states_t  runningState = ESP_OTA_IMG_VALID;
    inline bool                  runningKnown = true;     // false: no record in otadata (flashed over USB)
    inline const esp_partition_t* invalid     = nullptr;  // the one the bootloader gave up on
    inline int                   markedValid  = 0;        // esp_ota_mark_app_valid_cancel_rollback calls

    inline void reset() {
        if (written) { free(written); written = nullptr; }
        writtenLen = beginSize = 0;
        ended = aborted = bootSet = false;
        failBegin = failWrite = failEnd = failBoot = ESP_OK;
        runningState = ESP_OTA_IMG_VALID;
        runningKnown = true;
        invalid = nullptr;
        markedValid = 0;
    }
}

inline const esp_partition_t* esp_ota_get_running_partition() { return &OtaMock::running; }

inline esp_err_t esp_ota_get_state_partition(const esp_partition_t* p, esp_ota_img_states_t* state) {
    if (p == &OtaMock::running) {
        if (!OtaMock::runningKnown) return ESP_ERR_NOT_FOUND;
        *state = OtaMock::runningState;
        return ESP_OK;
    }
    if (p && p == OtaMock::invalid) { *state = ESP_OTA_IMG_ABORTED; return ESP_OK; }
    return ESP_ERR_NOT_FOUND;
}

inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() {
    OtaMock::markedValid++;
    OtaMock::runningState = ESP_OTA_IMG_VALID;
    return ESP_OK;
}

inline const esp_partition_t* esp_ota_get_last_invalid_partition() { return OtaMock::invalid; }

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
