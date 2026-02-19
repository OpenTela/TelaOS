/**
 * Evolution OS - Main Entry Point
 * PlatformIO / Arduino
 */

#include <Arduino.h>
#include <lvgl.h>
#include "esp_heap_caps.h"
#include "hal/display_hal.h"
#include "core/app_manager.h"
#include "core/state_store.h"
#include "ui/ui_task.h"
#include "ui/ui_engine.h"
#include "utils/task_queue.h"
#include "console/console.h"
#include "console/serial_transport.h"
#include "core/call_queue.h"
#include "ble/ble_bridge.h"
#include "ble/bin_transfer.h"
#include "ble/bin_receive.h"
#include "ui/ui_shade.h"
#include "native/counter_app.h"

// BOOT button — PIN_BOOT from board_config.h
static volatile bool g_buttonPressed = false;
static unsigned long g_lastButtonTime = 0;

void IRAM_ATTR buttonISR() {
    unsigned long now = millis();
    if (now - g_lastButtonTime > 300) {
        g_buttonPressed = true;
        g_lastButtonTime = now;
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    
    Serial.println("\n========================================");
    Serial.println("  Evolution OS v1.0");
    Serial.println("  PlatformIO Edition");
    Serial.println("========================================\n");
    
    // Check PSRAM availability
    Serial.printf("[PSRAM] Total: %u bytes\n", ESP.getPsramSize());
    Serial.printf("[PSRAM] Free:  %u bytes\n", ESP.getFreePsram());
    void* psramTest = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (psramTest) {
        Serial.println("[PSRAM] Allocation test: OK");
        heap_caps_free(psramTest);
    } else {
        Serial.println("[PSRAM] Allocation test: FAILED!");
    }
    
    // BLE is initialized on-demand when app requires it (via <bluetooth/> tag)
    Serial.printf("[Heap] Before Display: %d bytes free\n", ESP.getFreeHeap());
    
    if (!display_init()) {
        Serial.println("[ERROR] Display init failed!");
        return;
    }
    
    Serial.printf("[Heap] After Display: %d bytes free\n", ESP.getFreeHeap());
    
    State::store().onChange([](const P::String& name, const VarValue& value) {
        P::String strVal = std::visit([](auto&& v) -> P::String {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, P::String>) return v;
            else if constexpr (std::is_same_v<T, int>) return P::String(std::to_string(v).c_str());
            else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, float>) { char buf[32]; snprintf(buf, 32, "%.2f", v); return buf; }
            return "";
        }, value);
        ui_update_bindings(name.c_str(), strVal.c_str());
    });
    
    display_lock();
    ui_engine().init();
    Shade::init();
    display_unlock();
    
    App::Manager::instance().init();
    
    #ifdef PIN_BOOT
        pinMode(PIN_BOOT, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(PIN_BOOT), buttonISR, FALLING);
    #endif

    Serial.println("\n[Ready] Evolution OS running!");
    Serial.println("Press BOOT button to return to launcher\n");
}

void loop() {
    // Serial commands via SerialTransport
    static char cmdBuf[256];
    static int cmdPos = 0;
    static int cmdId = 0;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdPos > 0) {
                cmdBuf[cmdPos] = '\0';
                auto result = Console::exec(cmdBuf);
                Console::SerialTransport::instance().sendResult(++cmdId, result);
                cmdPos = 0;
            }
        } else if (cmdPos < (int)sizeof(cmdBuf) - 1) {
            cmdBuf[cmdPos++] = c;
        }
    }
    
    if (g_buttonPressed) {
        g_buttonPressed = false;
        auto& mgr = App::Manager::instance();
        if (!mgr.inLauncher()) {
            Serial.println("[BOOT] Returning to launcher...");
            mgr.returnToLauncher();
        }
    }
    
    CallQueue::process();
    
    // Process BLE only if initialized
    if (BLEBridge::isInitialized()) {
        BLEBridge::processBleQueue();
        
        // Send binary transfer chunks (screenshot, app pull, app list, etc.)
        if (BinTransfer::isInProgress()) {
            BinTransfer::sendNextChunk();
        }
        
        // Deferred save after BLE receive completes (LittleFS needs main loop stack)
        BinReceive::process();
    }
    
    App::Manager::instance().processPendingLaunch();
    
    display_lock();
    UI::processTasks();  // Process UI update tasks from state changes
    lv_timer_handler();
    display_unlock();
    
    delay(5);
}
