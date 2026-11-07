#pragma once
// NimBLE stub for TelaCI and the simulator: no radio. The objects are real, so the firmware's
// setup runs as on the watch, and NimBLEMock keeps what it asked for (security, the properties of
// each characteristic) and the server's callbacks, for a test to call as the host task would.
#include <cstdint>
#include <map>
#include <string>
#include "freertos/FreeRTOS.h"

class NimBLEServer;
class NimBLEService;
class NimBLECharacteristic;
class NimBLEAdvertising;
class NimBLEServerCallbacks;

class NimBLEConnInfo {
public:
    uint16_t handle = 1;
    bool encrypted = false;
    bool authenticated = false;
    bool bonded = false;
    uint16_t getConnHandle() const { return handle; }
    bool isEncrypted() const { return encrypted; }
    bool isAuthenticated() const { return authenticated; }
    bool isBonded() const { return bonded; }
};
class NimBLEAddress { public: std::string toString() { return ""; } };

class NimBLECharacteristicCallbacks {
public:
    virtual void onWrite(NimBLECharacteristic*, NimBLEConnInfo&) {}
    virtual ~NimBLECharacteristicCallbacks() = default;
};

class NimBLECharacteristic {
public:
    void setValue(const std::string&) {}
    void setValue(const uint8_t*, size_t) {}
    std::string getValue() { return ""; }
    void notify() {}
    void setCallbacks(NimBLECharacteristicCallbacks*) {}
};

class NimBLEServerCallbacks {
public:
    virtual void onConnect(NimBLEServer*, NimBLEConnInfo&) {}
    virtual void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) {}
    virtual uint32_t onPassKeyDisplay() { return 123456; }
    virtual void onAuthenticationComplete(NimBLEConnInfo&) {}
    virtual ~NimBLEServerCallbacks() = default;
};

struct NimBLEMock {
    bool bonding = false, mitm = false, sc = false;  // setSecurityAuth
    int ioCap = -1;                                  // setSecurityIOCap
    int securityStarted = 0;                         // startSecurity calls
    int disconnects = 0;
    int bonds = 0;
    std::map<std::string, uint32_t> properties;      // characteristic UUID -> NIMBLE_PROPERTY flags
    NimBLEServerCallbacks* serverCallbacks = nullptr;
};
inline NimBLEMock g_nimble_mock;

class NimBLEService {
public:
    NimBLECharacteristic* createCharacteristic(const char* uuid, uint32_t properties) {
        g_nimble_mock.properties[uuid] = properties;
        return &chars_[uuid];
    }
    void start() {}

private:
    std::map<std::string, NimBLECharacteristic> chars_;
};

class NimBLEAdvertising {
public:
    void addServiceUUID(const char*) {}
    void start() {}
    void setName(const char*) {}
};

class NimBLEServer {
public:
    NimBLEService* createService(const char*) { return &service_; }
    void setCallbacks(NimBLEServerCallbacks* cb) { g_nimble_mock.serverCallbacks = cb; }
    NimBLEAdvertising* getAdvertising() { return &advertising_; }
    bool disconnect(uint16_t, uint8_t = 0) {
        ++g_nimble_mock.disconnects;
        return true;
    }

private:
    NimBLEService service_;
    NimBLEAdvertising advertising_;
};

class NimBLEDevice {
public:
    static void init(const char*) {}
    static NimBLEServer* createServer() {
        static NimBLEServer server;
        return &server;
    }
    static void startAdvertising() {}
    static void setPower(int) {}
    static NimBLEAddress getAddress() { return {}; }
    static NimBLEAdvertising* getAdvertising() {
        static NimBLEAdvertising advertising;
        return &advertising;
    }
    static void deinit(bool) {}
    static void setSecurityAuth(bool bonding, bool mitm, bool sc) {
        g_nimble_mock.bonding = bonding;
        g_nimble_mock.mitm = mitm;
        g_nimble_mock.sc = sc;
    }
    static void setSecurityIOCap(uint8_t cap) { g_nimble_mock.ioCap = cap; }
    static bool startSecurity(uint16_t, int* = nullptr) {
        ++g_nimble_mock.securityStarted;
        return true;
    }
    static int getNumBonds() { return g_nimble_mock.bonds; }
    static bool deleteAllBonds() {
        g_nimble_mock.bonds = 0;
        return true;
    }
};

namespace NIMBLE_PROPERTY {
    constexpr uint32_t READ = 1;
    constexpr uint32_t WRITE = 2;
    constexpr uint32_t NOTIFY = 4;
    constexpr uint32_t WRITE_NR = 8;
    constexpr uint32_t READ_ENC = 0x10;
    constexpr uint32_t READ_AUTHEN = 0x20;
    constexpr uint32_t WRITE_ENC = 0x40;
    constexpr uint32_t WRITE_AUTHEN = 0x80;
}

#define BLE_HS_IO_DISPLAY_ONLY 0
#define ESP_PWR_LVL_P3 3
