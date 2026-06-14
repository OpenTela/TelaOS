#include "ble/bin_stream.h"
#include "utils/log_config.h"

static const char* TAG = "BinStream";

void BinStream::begin(uint32_t expectedSize, WriteFn write, ErrorFn onError) {
    write_         = std::move(write);
    onError_       = std::move(onError);
    expectedSize_  = expectedSize;
    received_      = 0;
    expectedChunk_ = 0;
    active_        = true;
}

void BinStream::reset() {
    active_        = false;
    received_      = 0;
    expectedChunk_ = 0;
    write_         = nullptr;
    onError_       = nullptr;
}

void BinStream::onChunk(const uint8_t* data, uint32_t len) {
    if (!active_) return;

    // On any framing error: disarm first, then invoke a local copy of the
    // callback. This avoids mutating onError_ (via the consumer's cleanup()
    // calling reset()) while it is still executing.
    auto fail = [this](Error e) {
        ErrorFn cb = onError_;
        reset();
        if (cb) cb(e);
    };

    // OBSOLETE: end-of-transfer marker [0xFF, 0xFF] (chunk_id 0xFFFF, no payload).
    // The marker is removed from the protocol (TelaPhone PR #1) - completion is
    // tracked by byte count. Kept only as a compat shim for pre-PR#1 clients
    // still in the field. Remove this branch (and its bin_stream test) once the
    // field has migrated.
    if (len == 2 && data[0] == 0xFF && data[1] == 0xFF) {
        return;
    }

    // 2B header + at least 1 data byte
    if (len < 3) {
        LOG_W(Log::BLE, "BinStream: chunk too small: %u bytes", len);
        fail(Error::TooSmall);
        return;
    }

    uint16_t chunkId       = data[0] | (data[1] << 8);
    const uint8_t* payload = data + 2;
    uint32_t payloadLen    = len - 2;

    if (chunkId != expectedChunk_) {
        LOG_E(Log::BLE, "BinStream: out of order: got %u, expected %u", chunkId, expectedChunk_);
        fail(Error::OutOfOrder);
        return;
    }

    if (received_ + payloadLen > expectedSize_) {
        LOG_E(Log::BLE, "BinStream: overflow: %u + %u > %u", received_, payloadLen, expectedSize_);
        fail(Error::Overflow);
        return;
    }

    if (write_) write_(payload, payloadLen);

    received_ += payloadLen;
    expectedChunk_++;
}
