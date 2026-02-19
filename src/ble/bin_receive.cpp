#include "ble/bin_receive.h"
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

static uint8_t*   s_buffer = nullptr;
static uint32_t   s_expectedSize = 0;
static uint32_t   s_received = 0;
static uint16_t   s_expectedChunk = 0;
static bool       s_active = false;
static char       s_appName[32] = {};
static char       s_fileName[48] = {};

// Multi-file mode
static const uint32_t MAX_MULTI_FILES = 16;
static FileEntry  s_files[MAX_MULTI_FILES];
static uint32_t   s_fileCount = 0;
static bool       s_multiMode = false;
static bool       s_readyToSave = false;  // deferred save — BLE callback sets, main loop processes

// ─── validation ───────────────────────────────────────────────────────────

/// Simple XML validation: check that <app> and </app> are present and balanced
static bool validateHtml(const uint8_t* data, uint32_t size) {
    // Need null-terminated string for strstr
    // Buffer was allocated with expectedSize, add terminator temporarily
    // Actually safer: use memmem or manual search
    
    // Search for "<app" in buffer
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

    // Search for "</app>" in buffer
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
    // Size check
    if (s_received != s_expectedSize) {
        LOG_E(Log::APP, "Size mismatch: got %u, expected %u", s_received, s_expectedSize);
        return false;
    }

    if (s_multiMode) {
        // Validate each .html file in the blob
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

    // Single file: HTML validation
    const char* ext = strrchr(s_fileName, '.');
    if (ext && strcmp(ext, ".html") == 0) {
        return validateHtml(s_buffer, s_received);
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

    // Create resources/ subdirectory if needed
    if (strncmp(fileName, "resources/", 10) == 0) {
        char resDir[80];
        snprintf(resDir, sizeof(resDir), SYS_APPS "%s/resources", appName);
        ensureDir(resDir);
    }

    char fullPath[128];
    // Rename legacy app.html → appname.bax
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
    return saveOneFile(s_appName, s_fileName, s_buffer, s_received);
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
    s_active = false;
    s_readyToSave = false;
}

/// Send result back on text channel
static void sendResult(bool ok, const char* msg) {
    // JSON on text char: {"cmd":"push","status":"ok"/"error","msg":"..."}
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"push\",\"status\":\"%s\",\"msg\":\"%s\"}",
             ok ? "ok" : "error", msg);
    BLEBridge::send(P::String(buf));
}

// ─── public API ───────────────────────────────────────────────────────────

bool start(const char* appName, const char* fileName, uint32_t expectedSize) {
    if (s_active) {
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

    // Sanitize app name: transliterate, lowercase, clean
    NameGen::sanitize(s_appName, appName, sizeof(s_appName));
    strncpy(s_fileName, fileName, sizeof(s_fileName) - 1);
    s_expectedSize = expectedSize;
    s_received = 0;
    s_expectedChunk = 0;
    s_multiMode = false;
    s_fileCount = 0;
    s_active = true;

    LOG_I(Log::BLE, "BinReceive start: %s/%s, %u bytes", appName, fileName, expectedSize);
    return true;
}

bool startMulti(const char* appName, const FileEntry* files, uint32_t fileCount, uint32_t totalSize) {
    if (s_active) {
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
    s_expectedSize = totalSize;
    s_received = 0;
    s_expectedChunk = 0;
    s_multiMode = true;
    s_active = true;

    LOG_I(Log::BLE, "BinReceive startMulti: %s, %u files, %u bytes", appName, fileCount, totalSize);
    return true;
}

void onChunk(const uint8_t* data, uint32_t len) {
    if (!s_active || !s_buffer) return;

    // Need at least 2 bytes for chunk header
    if (len < 3) {
        LOG_W(Log::BLE, "Chunk too small: %u bytes", len);
        return;
    }

    // Parse chunk_id (2B LE)
    uint16_t chunkId = data[0] | (data[1] << 8);
    const uint8_t* payload = data + 2;
    uint32_t payloadLen = len - 2;

    // Sequence check
    if (chunkId != s_expectedChunk) {
        LOG_E(Log::BLE, "Chunk out of order: got %u, expected %u", chunkId, s_expectedChunk);
        sendResult(false, "chunk sequence error");
        cleanup();
        return;
    }

    // Overflow check
    if (s_received + payloadLen > s_expectedSize) {
        LOG_E(Log::BLE, "Buffer overflow: %u + %u > %u", s_received, payloadLen, s_expectedSize);
        sendResult(false, "size overflow");
        cleanup();
        return;
    }

    memcpy(s_buffer + s_received, payload, payloadLen);
    s_received += payloadLen;
    s_expectedChunk++;

    if (s_expectedChunk % 50 == 0) {
        LOG_D(Log::BLE, "BinReceive chunk %u, %u/%u bytes", chunkId, s_received, s_expectedSize);
    }

    // All bytes received — defer save to main loop (BLE stack too small for LittleFS)
    if (s_received >= s_expectedSize) {
        LOG_I(Log::BLE, "BinReceive complete, %u bytes — saving deferred", s_received);
        s_readyToSave = true;
    }
}

bool isInProgress() {
    return s_active;
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

    cleanup();

    // Refresh launcher so new/updated app appears immediately
    if (saved) {
        App::Manager::instance().refreshApps();
    }
}

void cancel() {
    if (s_active) {
        LOG_W(Log::BLE, "BinReceive cancelled");
        cleanup();
    }
}

} // namespace BinReceive
