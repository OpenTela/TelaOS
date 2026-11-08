#pragma once
// NimBLE as TelaCI and the simulator have it: no radio. What the firmware creates is there (one
// object of each kind, doing nothing), so it never calls a member of a null pointer: UBSan
// (sim/tools/sim_asan.py) reports that, and the firmware's own null checks stay as they are
#include <cstdint>
#include <string>
#include "freertos/FreeRTOS.h"

class NimBLEServer;
class NimBLEService;
class NimBLECharacteristic;
class NimBLEAdvertising;
class NimBLEConnInfo {};
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

class NimBLEService {
public:
    NimBLECharacteristic* createCharacteristic(const char*, uint32_t) {
        static NimBLECharacteristic c;
        return &c;
    }
    void start() {}
};

class NimBLEServerCallbacks {
public:
    virtual void onConnect(NimBLEServer*, NimBLEConnInfo&) {}
    virtual void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) {}
    virtual ~NimBLEServerCallbacks() = default;
};

class NimBLEAdvertising {
public:
    void addServiceUUID(const char*) {}
    void start() {}
    void setName(const char*) {}
};

class NimBLEServer {
public:
    NimBLEService* createService(const char*) {
        static NimBLEService s;
        return &s;
    }
    void setCallbacks(NimBLEServerCallbacks* c) { callbacks = c; }
    NimBLEAdvertising* getAdvertising();
    NimBLEServerCallbacks* callbacks = nullptr;  // tests connect a client: callbacks->onConnect()
};

class NimBLEDevice {
public:
    static void init(const char*) {}
    static NimBLEServer* createServer() {
        static NimBLEServer s;
        return &s;
    }
    static void startAdvertising() {}
    static void setPower(int) {}
    static NimBLEAddress getAddress() { return {}; }
    static NimBLEAdvertising* getAdvertising() {
        static NimBLEAdvertising a;
        return &a;
    }
    static void deinit(bool) {}
};

inline NimBLEAdvertising* NimBLEServer::getAdvertising() { return NimBLEDevice::getAdvertising(); }

namespace NIMBLE_PROPERTY {
    constexpr uint32_t READ = 1;
    constexpr uint32_t WRITE = 2;
    constexpr uint32_t NOTIFY = 4;
    constexpr uint32_t WRITE_NR = 8;
}

#define ESP_PWR_LVL_P3 3
