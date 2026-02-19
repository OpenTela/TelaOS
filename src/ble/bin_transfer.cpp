#include "ble/bin_transfer.h"
#include "ble/ble_bridge.h"
#include "utils/log_config.h"
#include <esp_heap_caps.h>
#include <Arduino.h>
#include <cstring>

static const char* TAG = "BinTransfer";

namespace BinTransfer {

static const uint32_t CHUNK_DATA_SIZE = 250;  // NimBLE max notify (252) - 2B header

static uint8_t*  s_buffer = nullptr;
static uint32_t  s_totalSize = 0;
static uint32_t  s_offset = 0;
static uint16_t  s_chunkId = 0;
static bool      s_active = false;

void start(uint8_t* buffer, uint32_t size) {
    if (s_active) cancel();

    s_buffer = buffer;
    s_totalSize = size;
    s_offset = 0;
    s_chunkId = 0;
    s_active = true;

    LOG_I(Log::BLE, "BinTransfer start: %u bytes, buf=%p, ble_connected=%d",
          size, buffer, BLEBridge::isConnected() ? 1 : 0);
}

bool sendNextChunk() {
    if (!s_active || !s_buffer) return false;

    // All data sent — done
    if (s_offset >= s_totalSize) {
        LOG_I(Log::BLE, "BinTransfer done: %u chunks, %u bytes", s_chunkId, s_totalSize);

        heap_caps_free(s_buffer);
        s_buffer = nullptr;
        s_active = false;
        return false;
    }

    uint32_t remaining = s_totalSize - s_offset;
    uint32_t dataSize = (remaining < CHUNK_DATA_SIZE) ? remaining : CHUNK_DATA_SIZE;

    // [2B chunk_id LE][data]
    uint8_t packet[252];
    packet[0] = s_chunkId & 0xFF;
    packet[1] = (s_chunkId >> 8) & 0xFF;
    memcpy(packet + 2, s_buffer + s_offset, dataSize);

    bool ok = BLEBridge::sendBinary(packet, 2 + dataSize);
    LOG_D(Log::BLE, "BinTransfer chunk[%u]: %u bytes, offset=%u/%u, sent=%d",
          s_chunkId, dataSize, s_offset, s_totalSize, ok ? 1 : 0);

    if (!ok) {
        return true;  // retry next loop
    }

    delay(15);

    s_offset += dataSize;
    s_chunkId++;
    return true;
}

bool isInProgress() {
    return s_active;
}

void cancel() {
    if (s_active) {
        LOG_W(Log::BLE, "BinTransfer cancelled");
        if (s_buffer) {
            heap_caps_free(s_buffer);
            s_buffer = nullptr;
        }
        s_active = false;
    }
}

} // namespace BinTransfer
