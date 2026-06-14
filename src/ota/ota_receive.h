#pragma once

#include <cstdint>

/**
 * OtaReceive - receive a full firmware image over BLE BIN_CHAR and flash it
 * to the inactive OTA partition (app0 / app1).
 *
 * The whole image is buffered in PSRAM, then written in a single
 * esp_ota_write(). An optional SHA-256 (hex, lowercase/uppercase) of the
 * firmware is verified before anything is written to flash.
 *
 * Wire protocol:
 *   command (RX, text):  [id, "sys", "ota", [raw_size, comp_size, "<sha256_hex>"]]
 *                        comp_size == 0  -> image sent uncompressed
 *                        comp_size  > 0  -> image sent as one LZ4 block (block format,
 *                                          same codec as the screenshot path)
 *   data    (BIN_CHAR):  [2B chunk_id LE][payload]   - same framing as BinReceive
 *
 * The SHA-256 (if given) is verified against the RAW (decompressed) image -
 * i.e. exactly the bytes written to flash. Completes when the received byte
 * count == comp_size (or raw_size when uncompressed). On success the device
 * reboots into the freshly written partition.
 *
 * NOTE: kept separate from BinReceive on purpose - OTA writes to flash, not
 * LittleFS. The wire transport itself is shared via BinStream.
 */
namespace OtaReceive {

/// Allocate buffers and arm the transfer.
/// @param rawSize    size of the decompressed image (== bytes written to flash)
/// @param compSize   size of the compressed stream, or 0 for no compression
/// @param sha256Hex  64-char hex digest of the RAW image, or "" to skip
/// @return false on invalid sizes or PSRAM allocation failure
bool start(uint32_t rawSize, uint32_t compSize, const char* sha256Hex);

/// Feed one BIN_CHAR write. Cheap: sequence-check + memcpy into the buffer.
/// Called from the BLE write callback context.
void onChunk(const uint8_t* data, uint32_t len);

/// Heavy work (hash verify + esp_ota_* + reboot), deferred to the main loop.
void process();

/// True while a transfer is armed/in progress (used to route BIN_CHAR writes).
bool isInProgress();

/// Abort and free the buffer.
void cancel();

} // namespace OtaReceive
