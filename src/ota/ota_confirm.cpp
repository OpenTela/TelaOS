#include "ota/ota_confirm.h"
#include "utils/log_config.h"
#include <Arduino.h>
#include <esp_ota_ops.h>

static const char* TAG = "OtaConfirm";

// Arduino-ESP32 (esp32-hal-misc.c, initArduino): true = the firmware confirms a pending OTA
// itself. TelaOS does, in OtaConfirm::tick.
extern "C" bool verifyRollbackLater() { return true; }

namespace OtaConfirm {

static bool s_pending = false;
static const char* s_invalid = nullptr;  // its label: the partition table lives as long as the firmware

static bool runningState(esp_ota_img_states_t& st) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    return running && esp_ota_get_state_partition(running, &st) == ESP_OK;
}

void begin() {
    esp_ota_img_states_t st;
    s_pending = runningState(st) && st == ESP_OTA_IMG_PENDING_VERIFY;
    if (s_pending) {
        LOG_W(Log::APP, "New firmware: kept after %u s of running, a reset before that brings back the previous one",
              (unsigned)(kHealthyMs / 1000));
    }
    // Once: it checks the whole image in that partition, ~2.5 MB of flash
    const esp_partition_t* bad = esp_ota_get_last_invalid_partition();
    s_invalid = bad ? bad->label : nullptr;
    if (s_invalid)
        LOG_W(Log::APP, "The firmware in %s was rolled back", s_invalid);
}

void tick(uint32_t nowMs) {
    if (!s_pending || nowMs < kHealthyMs)
        return;
    s_pending = false;
    const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK)
        LOG_I(Log::APP, "New firmware confirmed after %u s", (unsigned)(nowMs / 1000));
    else
        LOG_E(Log::APP, "Can't confirm the new firmware: %s", esp_err_to_name(err));
}

const char* state() {
    esp_ota_img_states_t st;
    if (!runningState(st))
        return "none";
    switch (st) {
        case ESP_OTA_IMG_NEW:            return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending";
        case ESP_OTA_IMG_VALID:          return "valid";
        case ESP_OTA_IMG_INVALID:        return "invalid";
        case ESP_OTA_IMG_ABORTED:        return "aborted";
        default:                         return "undefined";
    }
}

const char* invalidPartition() { return s_invalid; }

} // namespace OtaConfirm
