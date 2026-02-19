#pragma once

#include <cstdint>
#include "utils/psram_alloc.h"

/**
 * BinReceive — receives binary chunks over BLE BIN_CHAR
 *
 * Single file:
 *   BinReceive::start("weather", "weather.bax", 1234)
 *
 * Multi file (app push *):
 *   BinReceive::startMulti("weather", {{"weather.bax",1984},{"icon.png",512}}, 2496)
 *   Blob is split by sizes and each file saved separately.
 *
 * Chunk format: [2B chunk_id LE][up to 250B data]
 * Completes when received bytes == expected size.
 */
namespace BinReceive {

struct FileEntry {
    char name[48];
    uint32_t size;
};

/// Start single-file receive
bool start(const char* appName, const char* fileName, uint32_t expectedSize);

/// Start multi-file receive (app push *)
bool startMulti(const char* appName, const FileEntry* files, uint32_t fileCount, uint32_t totalSize);

/// Called from BIN_CHAR write callback
void onChunk(const uint8_t* data, uint32_t len);

/// Process deferred save (call from main loop)
void process();

bool isInProgress();
void cancel();

} // namespace BinReceive
