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
void deinit();  // from the LVGL task, the display locked (the shade's BT button)
bool isInitialized();
bool isConnected();
void processBleQueue();

// Pairing with a code (bluetooth.pairing): from the next init(). Then only an encrypted link that
// paired with the code the watch shows reads and writes; a bonded phone needs no code again
void setPairing(bool on);
bool pairing();
bool isSecure();      // the link now is encrypted and authenticated
int bonds();          // phones the watch remembers; -1 with BLE off
bool forgetBonds();   // all of them: each pairs with a code again

bool send(const P::String& data);
bool sendBinary(const uint8_t* data, size_t len);

int sendFetchRequest(const char* method, const char* url, const char* body, 
                     bool authorize, const char* format, 
                     const P::Array<P::String>& fields,
                     ResponseCallback callback);

} // namespace BLEBridge
