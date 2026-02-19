#pragma once

#include <NimBLEDevice.h>
#include "utils/psram_json.h"
#include <functional>
#include "utils/psram_alloc.h"




/**
 * BLE Bridge - transport layer for BLE communication
 * Commands are processed by Console module.
 */

namespace BLEBridge {

// Callback for HTTP fetch responses
using ResponseCallback = std::function<void(int status, const P::String& body)>;

// API
void init(const char* deviceName = "FutureClock");
void deinit();
bool isInitialized();
bool isConnected();
void processBleQueue();

bool send(const P::String& data);
bool sendBinary(const uint8_t* data, size_t len);

int sendFetchRequest(const char* method, const char* url, const char* body, 
                     bool authorize, const char* format, 
                     const P::Array<P::String>& fields,
                     ResponseCallback callback);

} // namespace BLEBridge
