#include "ble/bin_receive.h"
#include "ble/bin_stream.h"
#include "ble/ble_bridge.h"
#include "core/sys_paths.h"
#include "core/app_manager.h"
#include "utils/log_config.h"
#include "utils/name_gen.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <LittleFS.h>
#include <cstring>

static const char* TAG = "BinReceive";

namespace BinReceive {

static BinStream  s_stream;                // shared BIN transport
static uint8_t*   s_buffer = nullptr;
static char       s_appName[32] = {};
static char       s_fileName[48] = {};

// Multi-file mode
static const uint32_t MAX_MULTI_FILES = 16;
static FileEntry  s_files[MAX_MULTI_FILES];
static uint32_t   s_fileCount = 0;
static bool       s_multiMode = false;
static bool       s_readyToSave = false;  // deferred save — BLE callback sets, main loop processes

// forward
static void cleanup();

// ─── validation ───────────────────────────────────────────────────────────

/// Simple XML validation: check that <app> and </app> are present and balanced
static bool validateHtml(const uint8_t* data, uint32_t size) {
    const char* openTag = nullptr;
    for (uint32_t i = 0; i + 4 <= size; i++) {
        if (memcmp(data + i, "<app", 4) == 0) {
            openTag = (const char*)(data + i);
            break;
        }
    }
    if (!openTag) {
        LOG_E(Log::APP, "Validate: missing <app>");
        return false;
    }

    const char* closeTag = nullptr;
    for (uint32_t i = 0; i + 6 <= size; i++) {
        if (memcmp(data + i, "</app>", 6) == 0) {
            closeTag = (const char*)(data + i);
            break;
        }
    }
    if (!closeTag) {
        LOG_E(Log::APP, "Validate: missing </app>");
        return false;
    }

    if (closeTag <= openTag) {
        LOG_E(Log::APP, "Validate: </app> before <app>");
        return false;
    }

    return true;
}

/// Check file validity after receive
static bool validate() {
    uint32_t received = s_stream.received();
    uint32_t expected = s_stream.expected();

    if (received != expected) {
        LOG_E(Log::APP, "Size mismatch: got %u, expected %u", received, expected);
        return false;
    }

    if (s_multiMode) {
        uint32_t offset = 0;
        for (uint32_t i = 0; i < s_fileCount; i++) {
            const char* ext = strrchr(s_files[i].name, '.');
            if (ext && strcmp(ext, ".html") == 0) {
                if (!validateHtml(s_buffer + offset, s_files[i].size)) {
                    LOG_E(Log::APP, "Validate failed: %s", s_files[i].name);
                    return false;
                }
            }
            offset += s_files[i].size;
        }
        return true;
    }

    const char* ext = strrchr(s_fileName, '.');
    if (ext && strcmp(ext, ".html") == 0) {
        return validateHtml(s_buffer, received);
    }

    return true;
}

// ─── file save ────────────────────────────────────────────────────────────

static void ensureDir(const char* path) {
    if (!LittleFS.exists(path)) {
        LittleFS.mkdir(path);
    }
}

static bool saveOneFile(const char* appName, const char* fileName, const uint8_t* data, uint32_t size) {
    char dirPath[64];
    snprintf(dirPath, sizeof(dirPath), SYS_APPS "%s", appName);
    ensureDir(dirPath);

    if (strncmp(fileName, "resources/", 10) == 0) {
        char resDir[80];
        snprintf(resDir, sizeof(resDir), SYS_APPS "%s/resources", appName);
        ensureDir(resDir);
    }

    char fullPath[128];
    if (strcmp(fileName, "app.html") == 0) {
        snprintf(fullPath, sizeof(fullPath), SYS_APPS "%s/%s.bax", appName, appName);
    } else {
        snprintf(fullPath, sizeof(fullPath), SYS_APPS "%s/%s", appName, fileName);
    }

    File f = LittleFS.open(fullPath, "w");
    if (!f) {
        LOG_E(Log::APP, "Failed to open %s for writing", fullPath);
        return false;
    }

    size_t written = f.write(data, size);
    f.close();

    if (written != size) {
        LOG_E(Log::APP, "Write failed: %u/%u bytes", (unsigned)written, size);
        return false;
    }

    LOG_I(Log::APP, "Saved %s (%u bytes)", fullPath, size);
    return true;
}

static bool saveFile() {
    return saveOneFile(s_appName, s_fileName, s_buffer, s_stream.received());
}

static bool saveMultiFiles() {
    uint32_t offset = 0;
    uint32_t saved = 0;

    for (uint32_t i = 0; i < s_fileCount; i++) {
        if (saveOneFile(s_appName, s_files[i].name, s_buffer + offset, s_files[i].size)) {
            saved++;
        }
        offset += s_files[i].size;
    }

    LOG_I(Log::APP, "Multi save: %u/%u files", saved, s_fileCount);
    return (saved == s_fileCount);
}

// ─── cleanup ──────────────────────────────────────────────────────────────

static void cleanup() {
    if (s_buffer) {
        heap_caps_free(s_buffer);
        s_buffer = nullptr;
    }
    s_stream.reset();
    s_readyToSave = false;
}

/// Send result back on text channel
static void sendResult(bool ok, const char* msg) {
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"push\",\"status\":\"%s\",\"msg\":\"%s\"}",
             ok ? "ok" : "error", msg);
    BLEBridge::send(P::String(buf));
}

// --- transport wiring (shared BinStream) ---

/// Arm s_stream to memcpy chunks into s_buffer and report framing errors.
static void armStream(uint32_t totalSize) {
    s_stream.begin(
        totalSize,
        [](const uint8_t* data, uint32_t len) {
            memcpy(s_buffer + s_stream.received(), data, len);
        },
        [](BinStream::Error) {
            sendResult(false, "chunk framing error");
            cleanup();
        });
}

// ─── public API ───────────────────────────────────────────────────────────

bool start(const char* appName, const char* fileName, uint32_t expectedSize) {
    if (s_stream.isActive()) {
        LOG_W(Log::BLE, "BinReceive already active, cancelling");
        cancel();
    }

    if (expectedSize == 0 || expectedSize > 512 * 1024) {
        LOG_E(Log::BLE, "Invalid size: %u", expectedSize);
        return false;
    }

    s_buffer = (uint8_t*)heap_caps_malloc(expectedSize, MALLOC_CAP_SPIRAM);
    if (!s_buffer) {
        LOG_E(Log::BLE, "Failed to allocate %u bytes", expectedSize);
        return false;
    }

    NameGen::sanitize(s_appName, appName, sizeof(s_appName));
    strncpy(s_fileName, fileName, sizeof(s_fileName) - 1);
    s_multiMode = false;
    s_fileCount = 0;

    armStream(expectedSize);

    LOG_I(Log::BLE, "BinReceive start: %s/%s, %u bytes", appName, fileName, expectedSize);
    return true;
}

bool startMulti(const char* appName, const FileEntry* files, uint32_t fileCount, uint32_t totalSize) {
    if (s_stream.isActive()) {
        LOG_W(Log::BLE, "BinReceive already active, cancelling");
        cancel();
    }

    if (totalSize == 0 || totalSize > 512 * 1024) {
        LOG_E(Log::BLE, "Invalid total size: %u", totalSize);
        return false;
    }

    if (fileCount == 0 || fileCount > MAX_MULTI_FILES) {
        LOG_E(Log::BLE, "Invalid file count: %u", fileCount);
        return false;
    }

    s_buffer = (uint8_t*)heap_caps_malloc(totalSize, MALLOC_CAP_SPIRAM);
    if (!s_buffer) {
        LOG_E(Log::BLE, "Failed to allocate %u bytes", totalSize);
        return false;
    }

    NameGen::sanitize(s_appName, appName, sizeof(s_appName));
    memcpy(s_files, files, fileCount * sizeof(FileEntry));
    s_fileCount = fileCount;
    s_multiMode = true;

    armStream(totalSize);

    LOG_I(Log::BLE, "BinReceive startMulti: %s, %u files, %u bytes", appName, fileCount, totalSize);
    return true;
}

void onChunk(const uint8_t* data, uint32_t len) {
    if (!s_stream.isActive() || !s_buffer) return;

    s_stream.onChunk(data, len);

    if (s_stream.received() % (250 * 50) < 250) {
        LOG_D(Log::BLE, "BinReceive %u/%u bytes", s_stream.received(), s_stream.expected());
    }

    if (s_stream.isComplete()) {
        LOG_I(Log::BLE, "BinReceive complete, %u bytes - saving deferred", s_stream.received());
        s_readyToSave = true;
    }
}

bool isInProgress() {
    return s_stream.isActive();
}

void process() {
    if (!s_readyToSave) return;
    s_readyToSave = false;

    bool valid = validate();
    bool saved = false;
    if (valid) {
        saved = s_multiMode ? saveMultiFiles() : saveFile();
        if (s_multiMode) {
            char msg[48];
            snprintf(msg, sizeof(msg), "saved %u files", s_fileCount);
            sendResult(saved, saved ? msg : "write failed");
        } else {
            sendResult(saved, saved ? "saved" : "write failed");
        }
    } else {
        sendResult(false, "validation failed");
    }

    // Save app name before cleanup clears it
    P::String appName(s_appName);

    cleanup();

    // Refresh launcher so new/updated app appears immediately
    if (saved) {
        auto& mgr = App::Manager::instance();
        mgr.refreshApps();

        // Hot reload: if pushed app is currently running, relaunch it
        if (!mgr.inLauncher()) {
            const auto& cur = mgr.currentApp();
            size_t lastSlash = cur.rfind('/');
            size_t dot = cur.rfind('.');
            if (lastSlash != P::String::npos && dot != P::String::npos && dot > lastSlash) {
                P::String curName = cur.substr(lastSlash + 1, dot - lastSlash - 1);
                if (curName == appName) {
                    LOG_I(Log::APP, "Hot reload: relaunching %s", appName.c_str());
                    mgr.queueLaunch(appName);
                }
            }
        }
    }
}

void cancel() {
    if (s_stream.isActive() || s_buffer) {
        LOG_W(Log::BLE, "BinReceive cancelled");
        cleanup();
    }
}

} // namespace BinReceive
