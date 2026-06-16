#pragma once

#include <cstdint>
#include <functional>

/**
 * BinStream - shared BIN_CHAR receive transport.
 *
 * Owns the wire framing once, so consumers (app-push, OTA, ...) don't each
 * re-implement it:
 *   - chunk format: [2B chunk_id LE][payload]
 *   - strict in-order sequence check (any gap -> abort)
 *   - received / expected byte accounting
 *   - completion detection (received == expected)
 *
 * The consumer supplies a single WriteFn called for each chunk's payload, in
 * order. WriteFn runs in the BLE write-callback context, so it MUST be cheap
 * (e.g. memcpy into a buffer). Heavy work belongs in the consumer's own
 * deferred main-loop step, gated on isComplete().
 *
 * BinStream does not own the destination buffer and does no flushing/saving -
 * that is entirely the consumer's concern.
 */
class BinStream {
public:
    /// Payload sink. Called once per chunk, in order, with this chunk's bytes.
    using WriteFn = std::function<void(const uint8_t* data, uint32_t len)>;

    /// Error reasons reported via ErrorFn.
    enum class Error {
        TooSmall,       // chunk shorter than the 2B header + 1 data byte
        OutOfOrder,     // chunk_id != expected
        Overflow,       // would write past expectedSize
    };

    /// Optional error callback. If set, called when a chunk is rejected.
    /// After an error the stream is reset (not armed) - the consumer should
    /// clean up its own buffer.
    using ErrorFn = std::function<void(Error err)>;

    /// Arm the stream for a transfer of exactly expectedSize bytes.
    void begin(uint32_t expectedSize, WriteFn write, ErrorFn onError = nullptr);

    /// Feed one raw BIN_CHAR write ([2B id][payload]). Cheap.
    void onChunk(const uint8_t* data, uint32_t len);

    /// True once received >= expectedSize.
    bool isComplete() const { return active_ && received_ >= expectedSize_; }

    /// True while armed (between begin() and reset()/error).
    bool isActive() const { return active_; }

    uint32_t received() const { return received_; }
    uint32_t expected() const { return expectedSize_; }

    /// Disarm and forget state (does NOT touch the consumer's buffer).
    void reset();

private:
    WriteFn  write_;
    ErrorFn  onError_;
    uint32_t expectedSize_  = 0;
    uint32_t received_      = 0;
    uint16_t expectedChunk_ = 0;
    bool     active_        = false;
};
