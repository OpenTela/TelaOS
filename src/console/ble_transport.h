/**
 * BleTransport - BLE implementation of Console::Transport
 * 
 * - JSON responses via TX characteristic  
 * - Binary data via BIN characteristic (BinTransfer)
 */
#pragma once

#include "console/transport.h"
#include "ble/bin_transfer.h"
#include "ble/ble_bridge.h"
#include "utils/psram_json.h"

namespace Console {

class BleTransport : public Transport {
public:
    static BleTransport& instance() {
        static BleTransport inst;
        return inst;
    }
    
    void sendResult(int requestId, const Result& result) override {
        // Build v2 JSON response: [id, "ok"|"error", data|{code,message,http}]
        auto resp = PsramJsonDoc();
        JsonArray out = resp.to<JsonArray>();
        out.add(requestId);
        out.add(result.success ? "ok" : "error");
        
        if (result.success) {
            // Add data object + payload info if present
            JsonObject dataObj = out.add<JsonObject>();
            
            // Copy fields from result.data
            for (auto kv : result.data.as<JsonObjectConst>()) {
                dataObj[kv.key()] = kv.value();
            }
            
            // Add payload info so client knows to expect binary
            if (result.type != Console::ResponseType::Short && result.payloadSize > 0) {
                dataObj["bytes"] = result.payloadSize;
                dataObj["type"] = (result.type == Console::ResponseType::BinaryData) ? "binary" : "string";
            }
        } else {
            JsonObject errObj = out.add<JsonObject>();
            errObj["code"] = result.errorCode.c_str();
            errObj["message"] = result.errorMessage.c_str();
            if (result.httpCode > 0) {
                errObj["http"] = result.httpCode;
            }
        }
        
        P::String json;
        serializeJson(resp, json);
        BLEBridge::send(json);
        
        // Deliver payload via BinTransfer (both StringData and BinaryData)
        if (result.type != Console::ResponseType::Short && result.payload && result.payloadSize > 0) {
            BinTransfer::start(result.payload, result.payloadSize);
        }
    }
    
    void sendText(const char* text) override {
        // Wrap in JSON for BLE
        auto resp = PsramJsonDoc();
        resp["text"] = text;
        P::String json;
        serializeJson(resp, json);
        BLEBridge::send(json);
    }
    
    bool supportsBinary() const override { return true; }
    
    bool isConnected() const override { 
        return BLEBridge::isConnected(); 
    }
    
    const char* name() const override { return "BLE"; }
    
private:
    BleTransport() = default;
};

} // namespace Console
