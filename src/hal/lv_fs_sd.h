#pragma once

/**
 * lv_fs_sd.h — registers an LVGL filesystem drive letter 'A' mapped to the SD
 * mount ("/sd"), so LVGL assets can be loaded with paths like "A:/img/x.png".
 *
 * ('A' for nostalgia — removable media, like a floppy.)
 *
 * Read/write via stdio on the FATFS VFS. Registered once after lv_init().
 * No-op on host (LVGL_MOCK_ENABLED) builds.
 */
namespace LvFsSd {

void registerDrive();

} // namespace LvFsSd
