#pragma once

/**
 * usbhost.h — USB Host stack + Mass Storage Class driver, exposing a connected
 * flash drive as a FATFS volume at "/usb".
 *
 * Symmetric to Sd::: once mounted, standard POSIX (fopen/opendir/stat) and the
 * Lua usb.* API work on "/usb/..." exactly like sd.* works on "/sd/...".
 *
 * Lifecycle:
 *   - Usb::init() brings up the USB Host stack (idempotent). Not called at
 *     boot: the stack + tasks cost RAM, so it starts on first usb.* use.
 *   - The MSC driver's background task enumerates a plugged drive; our
 *     callback registers a VFS mount and isMounted() flips true.
 *   - Unplugging fires the DISCONNECTED event: VFS is unregistered, paths
 *     stop resolving. Re-plugging re-mounts automatically — hot-plug is
 *     event-driven here, better than SD's lazy probe.
 *
 * Hardware: ESP32-S3 only (USB-OTG PHY fixed on GPIO19 D- / GPIO20 D+).
 * Other targets and the mock build compile to no-ops.
 */

#include <cstdint>
#include <cstddef>

namespace Usb {

inline constexpr const char* kMountPoint = "/usb";

// Bring up USB Host + MSC class driver. Safe to call repeatedly. Returns
// false on init errors or unsupported platforms.
bool init();

// Tear everything down: unregister VFS, uninstall drivers, stop tasks.
bool deinit();

// True iff a drive is enumerated AND its FAT volume is registered with VFS
// (POSIX I/O on "/usb/..." works right now).
bool isMounted();

// Initialize the stack if needed and report whether a volume is available.
// Cheap when already mounted. Unlike Sd::ensureMounted this does not block
// waiting for enumeration — a just-plugged drive appears a moment later.
bool ensureMounted();

const char* mountPoint();

// Total / free bytes of the mounted volume. False if not mounted.
bool info(uint64_t& totalBytes, uint64_t& freeBytes);

} // namespace Usb
