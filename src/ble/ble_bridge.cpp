#include "ble/ble_bridge.h"
#include "ble/bin_receive.h"
#include "ble/bin_transfer.h"
#include "console/console.h"
#include "console/ble_transport.h"
#include "utils/log_config.h"
#include "esp_heap_caps.h"
#include <queue>
#include <map>

namespace BLEBridge {

static const char* TAG = "BLEBridge";
static const char* VERSION = "2.0.0";

// UUIDs
static const char* SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0";
static const char* TX_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef1";
static const char* RX_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef2";
static const char* BIN_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef3";

// Global state
static std::map<int, ResponseCallback> g_pendingRequests;
static int g_requestId = 0;

static NimBLEServer* g_server = nullptr;
static NimBLECharacteristic* g_txChar = nullptr;
static NimBLECharacteristic* g_rxChar = nullptr;
static NimBLECharacteristic* g_binChar = nullptr;
static bool g_connected = false;
static bool g_initialized = false;

static std::queue<P::String> g_bleMessageQueue;
static SemaphoreHandle_t g_bleQueueMutex = nullptr;

// Forward declaration
static void handleIncoming(const P::String& data);

// Server callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        g_connected = true;
        LOG_I(Log::BLE, "Client connected");
    }
    
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        g_connected = false;
        LOG_I(Log::BLE, "Client disconnected (reason: %d)", reason);
        NimBLEDevice::startAdvertising();
        LOG_I(Log::BLE, "Advertising restarted");
    }
};

// RX characteristic callbacks
class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        P::String value(pChar->getValue().c_str());
        if (!value.empty()) {
            LOG_D(Log::BLE, "RX: %d bytes", (int)value.length());
            
            if (g_bleQueueMutex) {
                xSemaphoreTake(g_bleQueueMutex, portMAX_DELAY);
                g_bleMessageQueue.push(value);
                xSemaphoreGive(g_bleQueueMutex);
            }
        }
    }
};

// BIN characteristic callbacks (binary data for push)
class BinRxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        auto val = pChar->getValue();
        if (val.length() > 0) {
            BinReceive::onChunk((const uint8_t*)val.data(), val.length());
        }
    }
};

static void handleIncoming(const P::String& data) {
    auto doc = PsramJsonDoc();
    DeserializationError error = deserializeJson(doc, data);
    
    if (error) {
        LOG_E(Log::BLE, "JSON parse error: %s", error.c_str());
        return;
    }
    
    LOG_D(Log::BLE, "RX: %s", data.c_str());
    
    // ─── fetch response from Python bridge: {id, status(int), body} ───
    if (doc["id"].is<int>() && doc["status"].is<int>()) {
        int id = doc["id"];
        int status = doc["status"];
        P::String body = doc["body"] | "";
        
        LOG_D(Log::BLE, "fetch response[%d] status=%d body=%d bytes", id, status, (int)body.length());
        
        if (g_pendingRequests.count(id)) {
            auto callback = g_pendingRequests[id];
            g_pendingRequests.erase(id);
            if (callback) callback(status, body);
        } else {
            LOG_W(Log::BLE, "fetch response[%d] no callback!", id);
        }
        return;
    }
    
    // ─── v2 command: [id, subsystem, cmd, args] ──────────────────────
    if (doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() < 3) {
            LOG_E(Log::BLE, "v2: need [id, sub, cmd, args]");
            return;
        }
        
        int id = arr[0] | 0;
        const char* subsystem = arr[1] | "";
        const char* cmd = arr[2] | "";
        
        // args is optional 4th element (array), default empty
        auto argsDoc = PsramJsonDoc();
        JsonArray args = argsDoc.to<JsonArray>();
        if (arr.size() >= 4 && arr[3].is<JsonArray>()) {
            for (auto v : arr[3].as<JsonArray>()) {
                args.add(v);
            }
        }
        
        LOG_I(Log::BLE, "v2: %s %s (id=%d)", subsystem, cmd, id);
        
        auto result = Console::exec(subsystem, cmd, args);
        
        if (id > 0) {
            Console::BleTransport::instance().sendResult(id, result);
        }
        return;
    }
    
    LOG_W(Log::BLE, "Unknown message format");
}

bool send(const P::String& data) {
    if (!g_connected || !g_txChar) {
        LOG_W(Log::BLE, "Not connected, cannot send");
        return false;
    }
    
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    LOG_D(Log::BLE, "TX attempt: %d bytes (heap: %d)", (int)data.length(), (int)freeHeap);
    
    if (freeHeap < 10000) {
        LOG_W(Log::BLE, "Low heap warning! May fail to send");
    }
    
    g_txChar->setValue(data);
    g_txChar->notify();
    
    LOG_D(Log::BLE, "TX: %d bytes (heap after: %d)", (int)data.length(), (int)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    return true;
}

bool sendBinary(const uint8_t* data, size_t len) {
    if (!g_connected || !g_binChar) {
        LOG_W(Log::BLE, "Not connected or BIN char not ready");
        return false;
    }
    
    if (len > 512) {
        LOG_E(Log::BLE, "BIN data too large: %d > 512", (int)len);
        return false;
    }
    
    g_binChar->setValue(data, len);
    g_binChar->notify();
    
    return true;
}

int sendFetchRequest(const char* method, const char* url, const char* body, 
                     bool authorize, const char* format, 
                     const P::Array<P::String>& fields,
                     ResponseCallback callback) {
    int id = ++g_requestId;
    
    LOG_D(Log::BLE, "fetch[%d] %s %s (fields=%d)", id, method, url, (int)fields.size());
    
    auto doc = PsramJsonDoc();
    doc["id"] = id;
    doc["method"] = method;
    doc["url"] = url;
    if (body && body[0]) {
        doc["body"] = body;
    }
    if (authorize) {
        doc["authorize"] = true;
    }
    if (format && format[0]) {
        doc["format"] = format;
    }
    if (!fields.empty()) {
        JsonArray arr = doc["fields"].to<JsonArray>();
        for (const auto& f : fields) {
            arr.add(f);
        }
    }
    
    P::String json;
    serializeJson(doc, json);
    
    g_pendingRequests[id] = callback;
    
    if (!send(json)) {
        g_pendingRequests.erase(id);
        LOG_E(Log::BLE, "fetch[%d] send failed!", id);
        return -1;
    }
    
    LOG_D(Log::BLE, "fetch[%d] sent %d bytes", id, (int)json.length());
    return id;
}

void init(const char* deviceName) {
    LOG_I(Log::BLE, "Initializing NimBLE...");
    
    if (!g_bleQueueMutex) {
        g_bleQueueMutex = xSemaphoreCreateMutex();
    }
    
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setPower(ESP_PWR_LVL_P3);
    
    P::String mac(NimBLEDevice::getAddress().toString().c_str());
    LOG_I(Log::BLE, "========================================");
    LOG_I(Log::BLE, "  BLE Device: %s", deviceName);
    LOG_I(Log::BLE, "  BLEBridge v%s", VERSION);
    LOG_I(Log::BLE, "  MAC Address: %s", mac.c_str());
    LOG_I(Log::BLE, "========================================");
    
    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(new ServerCallbacks());
    
    NimBLEService* service = g_server->createService(SERVICE_UUID);
    
    g_txChar = service->createCharacteristic(
        TX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
    );
    
    g_rxChar = service->createCharacteristic(
        RX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    g_rxChar->setCallbacks(new RxCallbacks());
    
    g_binChar = service->createCharacteristic(
        BIN_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    g_binChar->setCallbacks(new BinRxCallbacks());
    
    service->start();
    
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    // Don't add UUID to advertisement - it exceeds 31-byte limit
    // UUID is still discoverable after connection
    advertising->setName(deviceName);
    advertising->start();
    
    g_initialized = true;
    LOG_I(Log::BLE, "NimBLE ready, advertising as '%s'", deviceName);
}

bool isInitialized() {
    return g_initialized;
}

void deinit() {
    if (!g_initialized) return;
    
    LOG_I(Log::BLE, "Deinitializing BLE...");
    
    NimBLEDevice::deinit(true);
    
    g_server = nullptr;
    g_txChar = nullptr;
    g_rxChar = nullptr;
    g_binChar = nullptr;
    g_connected = false;
    g_initialized = false;
    
    LOG_I(Log::BLE, "BLE deinitialized");
}

bool isConnected() {
    return g_connected;
}

void processBleQueue() {
    if (!g_bleQueueMutex) return;
    
    P::String message;
    bool hasMessage = false;
    
    xSemaphoreTake(g_bleQueueMutex, portMAX_DELAY);
    if (!g_bleMessageQueue.empty()) {
        message = g_bleMessageQueue.front();
        g_bleMessageQueue.pop();
        hasMessage = true;
    }
    xSemaphoreGive(g_bleQueueMutex);
    
    if (hasMessage) {
        handleIncoming(message);
    }
}

} // namespace BLEBridge
