#pragma once

#include <cstdint>
#include <cstddef>

/**
 * BinTransfer — generic binary chunk sender over BLE BIN_CHAR
 *
 * Usage:
 *   BinTransfer::start(buffer, size);  // takes ownership of buffer (PSRAM)
 *   // in loop():
 *   BinTransfer::sendNextChunk();      // sends one chunk per call
 *
 * Chunk format: [2B chunk_id LE][up to 250B data]
 * Client knows total size from JSON response (_payloadSize field).
 */
namespace BinTransfer {

/// Start transfer. Buffer must be heap_caps_malloc'd (will be freed on completion).
void start(uint8_t* buffer, uint32_t size);

/// Send next chunk. Returns true if more to send.
bool sendNextChunk();

/// Is transfer in progress?
bool isInProgress();

/// Cancel and free buffer
void cancel();

} // namespace BinTransfer
