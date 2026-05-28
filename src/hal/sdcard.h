#pragma once

/**
 * sdcard.h — SD card over SPI, mounted as a FATFS VFS at "/sd".
 *
 * Symmetric to LittleFS at "/littlefs": once mounted, standard stdio
 * (fopen/opendir/stat) and the existing fs_exists() work on "/sd/..." paths.
 *
 * Board-gated: only boards that wire an SD slot call Sd::mountSpi() (from their
 * Device::mountSdCard() override). Boards without SD never mount and isMounted()
 * stays false. The card is optional and may be absent at boot.
 */

#include <cstdint>
#include <cstddef>

namespace Sd {

// Mount point for the SD FAT volume. Used by Lua sd.* and any path building.
inline constexpr const char* kMountPoint = "/sd";

// Initialize the given SPI bus and mount the card at kMountPoint.
// host: SPI host id (default SPI2). Safe to call when no card is present —
// returns false and leaves the system unmounted. Idempotent: a second call
// while already mounted returns true without touching the bus.
bool mountSpi(int csPin, int mosiPin, int misoPin, int sckPin, int spiHost = 1);

// Unmount and free the SPI bus. No-op if not mounted.
bool unmount();

bool isMounted();

const char* mountPoint();

// Total / free bytes of the mounted volume. Returns false if not mounted.
bool info(uint64_t& totalBytes, uint64_t& freeBytes);

} // namespace Sd
