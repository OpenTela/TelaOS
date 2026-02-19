/**
 * SerialTransport - Serial/USB implementation of Console::Transport
 * 
 * Text output + base64 binary.
 */
#pragma once

#include "console/transport.h"
#include <Arduino.h>
#include <base64.h>

namespace Console {

class SerialTransport : public Transport {
public:
    static SerialTransport& instance() {
        static SerialTransport inst;
        return inst;
    }
    
    void sendResult(int requestId, const Result& result) override {
        // Format: [id] status: message
        Serial.printf("[%d] %s", requestId, result.success ? "OK" : "ERROR");
        
        if (!result.success) {
            Serial.printf(" [%s]: %s", result.errorCode.c_str(), result.errorMessage.c_str());
            if (result.httpCode > 0) {
                Serial.printf(" (HTTP %d)", result.httpCode);
            }
        }
        Serial.println();
        
        // Print data fields
        JsonObjectConst obj = result.data.as<JsonObjectConst>();
        for (JsonPairConst kv : obj) {
            Serial.printf("  %s = ", kv.key().c_str());
            serializeJson(kv.value(), Serial);
            Serial.println();
        }
        
        // Deliver payload based on type
        if (result.payload && result.payloadSize > 0) {
            if (result.type == Console::ResponseType::BinaryData) {
                Serial.printf("  [binary: %u bytes]\n", result.payloadSize);
                Serial.print("  base64: ");
                String b64 = base64::encode(result.payload, result.payloadSize);
                Serial.println(b64);
            }
            else if (result.type == Console::ResponseType::StringData) {
                Serial.printf("  [string: %u bytes]\n", result.payloadSize);
                Serial.write(result.payload, result.payloadSize);
                Serial.println();
            }
        }
    }
    
    void sendText(const char* text) override {
        Serial.println(text);
    }
    
    bool supportsBinary() const override { return true; }
    
    bool isConnected() const override { return true; }
    
    const char* name() const override { return "Serial"; }
    
private:
    SerialTransport() = default;
};

} // namespace Console
