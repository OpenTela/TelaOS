#pragma once
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
    NimBLECharacteristic* createCharacteristic(const char*, uint32_t) { return nullptr; }
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
    NimBLEService* createService(const char*) { return nullptr; }
    void setCallbacks(NimBLEServerCallbacks*) {}
    NimBLEAdvertising* getAdvertising() { return nullptr; }
};

class NimBLEDevice {
public:
    static void init(const char*) {}
    static NimBLEServer* createServer() { return nullptr; }
    static void startAdvertising() {}
    static void setPower(int) {}
    static NimBLEAddress getAddress() { return {}; }
    static NimBLEAdvertising* getAdvertising() { return nullptr; }
    static void deinit(bool) {}
};

namespace NIMBLE_PROPERTY {
    constexpr uint32_t READ = 1;
    constexpr uint32_t WRITE = 2;
    constexpr uint32_t NOTIFY = 4;
    constexpr uint32_t WRITE_NR = 8;
}

#define ESP_PWR_LVL_P3 3
