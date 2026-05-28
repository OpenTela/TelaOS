#include "hal/sdcard.h"
#include "utils/log_config.h"

namespace Sd {

static const char* TAG = "SD";

#if defined(LVGL_MOCK_ENABLED)
// -------- Host build (CI mock): no hardware, stub everything --------
bool mountSpi(int, int, int, int, int) { return false; }
bool unmount() { return false; }
bool isMounted() { return false; }
const char* mountPoint() { return kMountPoint; }
bool info(uint64_t& t, uint64_t& f) { t = 0; f = 0; return false; }

#else
// -------- Device build (ESP-IDF FATFS over SPI) --------
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "ff.h"  // FATFS direct API (f_getfree) — works on IDF 4.4 and 5.x

static sdmmc_card_t* s_card    = nullptr;
static bool          s_mounted = false;
static int           s_spiHost = -1;

bool mountSpi(int csPin, int mosiPin, int misoPin, int sckPin, int spiHost) {
    if (s_mounted) return true;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = (spi_host_device_t) spiHost;

    spi_bus_config_t bus = {};
    bus.mosi_io_num     = mosiPin;
    bus.miso_io_num     = misoPin;
    bus.sclk_io_num     = sckPin;
    bus.quadwp_io_num   = -1;
    bus.quadhd_io_num   = -1;
    bus.max_transfer_sz = 4000;

    esp_err_t ret = spi_bus_initialize((spi_host_device_t) spiHost, &bus,
                                       SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE /* bus already up */) {
        LOG_E(Log::SD, "spi_bus_initialize failed: 0x%x", ret);
        return false;
    }

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = (gpio_num_t) csPin;
    slot.host_id = (spi_host_device_t) spiHost;

    esp_vfs_fat_mount_config_t mountCfg = {};
    mountCfg.format_if_mount_failed = false;   // never reformat a user's card
    mountCfg.max_files              = 5;
    mountCfg.allocation_unit_size   = 16 * 1024;

    ret = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &mountCfg, &s_card);
    if (ret != ESP_OK) {
        // ESP_FAIL = no card / unformatted; not a hard error, SD is optional.
        LOG_W(Log::SD, "mount failed (0x%x) — no card or unformatted", ret);
        spi_bus_free((spi_host_device_t) spiHost);
        return false;
    }

    s_spiHost = spiHost;
    s_mounted = true;
    uint64_t total = 0, freeB = 0;
    info(total, freeB);
    LOG_I(Log::SD, "mounted at %s (%llu MB total, %llu MB free)",
          kMountPoint, total >> 20, freeB >> 20);
    return true;
}

bool unmount() {
    if (!s_mounted) return false;
    esp_vfs_fat_sdcard_unmount(kMountPoint, s_card);
    if (s_spiHost >= 0) spi_bus_free((spi_host_device_t) s_spiHost);
    s_card = nullptr; s_mounted = false; s_spiHost = -1;
    LOG_I(Log::SD, "unmounted");
    return true;
}

bool isMounted() { return s_mounted; }
const char* mountPoint() { return kMountPoint; }

bool info(uint64_t& totalBytes, uint64_t& freeBytes) {
    totalBytes = 0; freeBytes = 0;
    if (!s_mounted) return false;
    // esp_vfs_fat_sdspi_mount registers FATFS at drive "0:" by default.
    // Use the FATFS API directly: it ships with ESP-IDF (both 4.4 and 5.x),
    // unlike POSIX statvfs (not in newlib for IDF 4.4) or esp_vfs_fat_info
    // (IDF 5.x only).
    FATFS* fs = nullptr;
    DWORD  freeClusters = 0;
    if (f_getfree("0:", &freeClusters, &fs) != FR_OK || fs == nullptr) return false;
    const uint64_t bytesPerCluster = (uint64_t) fs->csize * FF_MAX_SS;
    const uint64_t totalClusters   = (uint64_t)(fs->n_fatent - 2);
    totalBytes = totalClusters  * bytesPerCluster;
    freeBytes  = (uint64_t) freeClusters * bytesPerCluster;
    return true;
}

#endif  // LVGL_MOCK_ENABLED

} // namespace Sd
