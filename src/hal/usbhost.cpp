#include "hal/usbhost.h"
#include "utils/log_config.h"

namespace Usb {

static const char* TAG = "UsbHost";

#if defined(LVGL_MOCK_ENABLED) || !defined(CONFIG_IDF_TARGET_ESP32S3)
// -------- Host build (CI mock) or non-S3 target: stub everything --------
bool init()          { return false; }
bool deinit()        { return false; }
bool isMounted()     { return false; }
bool ensureMounted() { return false; }
const char* mountPoint() { return kMountPoint; }
bool info(uint64_t& t, uint64_t& f) { t = 0; f = 0; return false; }

#else
// -------- Device build (ESP32-S3, vendored usb_host_msc) --------
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// State. Mutated from the MSC background task, read from the Lua thread.
// Simple flags — the only cross-thread question is "mounted right now?".
static bool                     s_initialised = false;
static volatile bool            s_mounted     = false;
static msc_host_device_handle_t s_device      = nullptr;
static msc_host_vfs_handle_t    s_vfs         = nullptr;
static TaskHandle_t             s_libTask     = nullptr;

// Pump the USB Host library's event loop forever on its own task.
static void usbLibTask(void*) {
    while (true) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
    }
}

// MSC class callback — fired from the driver's background task. Keep it
// short: a slow callback stalls hot-plug detection.
static void mscEventCb(const msc_host_event_t* event, void*) {
    if (event->event == MSC_DEVICE_CONNECTED) {
        LOG_I(Log::SD, "USB MSC: device connected (addr=%d)", event->device.address);

        if (msc_host_install_device(event->device.address, &s_device) != ESP_OK) {
            LOG_E(Log::SD, "msc_host_install_device failed");
            return;
        }
        const esp_vfs_fat_mount_config_t mountCfg = {
            .format_if_mount_failed   = false,
            .max_files                = 4,
            .allocation_unit_size     = 4096,
            .disk_status_check_enable = false,
            .use_one_fat              = false,
        };
        if (msc_host_vfs_register(s_device, kMountPoint, &mountCfg, &s_vfs) != ESP_OK) {
            LOG_E(Log::SD, "msc_host_vfs_register failed");
            msc_host_uninstall_device(s_device);
            s_device = nullptr;
            return;
        }
        s_mounted = true;
        LOG_I(Log::SD, "USB MSC: mounted at %s", kMountPoint);
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        LOG_I(Log::SD, "USB MSC: device disconnected");
        s_mounted = false;
        if (s_vfs)    { msc_host_vfs_unregister(s_vfs);      s_vfs    = nullptr; }
        if (s_device) { msc_host_uninstall_device(s_device); s_device = nullptr; }
    }
}

bool init() {
    if (s_initialised) return true;

    const usb_host_config_t hostCfg = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LEVEL1,
    };
    if (usb_host_install(&hostCfg) != ESP_OK) {
        LOG_E(Log::SD, "usb_host_install failed");
        return false;
    }
    if (xTaskCreate(usbLibTask, "usbLib", 4096, nullptr, 4, &s_libTask) != pdPASS) {
        LOG_E(Log::SD, "usbLib task create failed");
        usb_host_uninstall();
        return false;
    }
    const msc_host_driver_config_t mscCfg = {
        .create_backround_task = true,   // [sic] upstream field name
        .task_priority         = 5,
        .stack_size            = 4096,
        .core_id               = 0,
        .callback              = mscEventCb,
        .callback_arg          = nullptr,
    };
    if (msc_host_install(&mscCfg) != ESP_OK) {
        LOG_E(Log::SD, "msc_host_install failed");
        vTaskDelete(s_libTask); s_libTask = nullptr;
        usb_host_uninstall();
        return false;
    }
    s_initialised = true;
    LOG_I(Log::SD, "USB Host + MSC initialised");
    return true;
}

bool deinit() {
    if (!s_initialised) return false;
    if (s_vfs)    { msc_host_vfs_unregister(s_vfs);      s_vfs    = nullptr; }
    if (s_device) { msc_host_uninstall_device(s_device); s_device = nullptr; }
    msc_host_uninstall();
    if (s_libTask) { vTaskDelete(s_libTask); s_libTask = nullptr; }
    usb_host_uninstall();
    s_mounted = false;
    s_initialised = false;
    return true;
}

bool isMounted() { return s_mounted; }

bool ensureMounted() {
    if (!s_initialised) init();
    return s_mounted;
}

const char* mountPoint() { return kMountPoint; }

bool info(uint64_t& totalBytes, uint64_t& freeBytes) {
    totalBytes = 0; freeBytes = 0;
    if (!s_mounted) return false;
    // SD occupies FATFS drive "0:", the MSC VFS registers as "1:".
    FATFS* fs;
    DWORD freeClusters;
    if (f_getfree("1:", &freeClusters, &fs) != FR_OK) return false;
    totalBytes = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
    freeBytes  = (uint64_t) freeClusters      * fs->csize * 512;
    return true;
}

#endif

} // namespace Usb
