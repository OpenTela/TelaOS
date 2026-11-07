#pragma once

#include <cstdint>

/**
 * OtaConfirm - a new firmware is kept only once it has run for kHealthyMs.
 *
 * After an OTA the bootloader starts the new firmware once as "pending" (ESP_OTA_IMG_PENDING_VERIFY).
 * Any reset while it is pending - a crash, a watchdog, a reboot - and the bootloader starts the
 * previous firmware again and marks this one invalid (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE in
 * Arduino-ESP32 2.0.17). Arduino-ESP32 would confirm it at once in initArduino(); verifyRollbackLater()
 * (ota_confirm.cpp) leaves that to tick(): confirmed after kHealthyMs of running.
 *
 * A firmware flashed over USB is not pending: nothing to confirm.
 *
 * Console: sys info "ota" (state()), "ota_invalid" (the partition rolled back from).
 */
namespace OtaConfirm {

constexpr uint32_t kHealthyMs = 20000;

/// At boot: the state of the running firmware and the partition rolled back from, logged
void begin();

/// From loop(): confirm a pending firmware once it has run kHealthyMs
void tick(uint32_t nowMs);

/// The running firmware in otadata: "pending", "valid", "new", "invalid", "aborted",
/// "undefined"; "none" if otadata has no record of it (flashed over USB)
const char* state();

/// The partition the bootloader gave up on, "app0" or "app1", or nullptr (as of begin())
const char* invalidPartition();

} // namespace OtaConfirm
