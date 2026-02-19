/**
 * Console v3 - Command processor
 * 
 * Transport-agnostic command execution.
 * Returns Result with optional binary payload.
 */
#pragma once

#include <ArduinoJson.h>
#include "utils/psram_alloc.h"

namespace Console {

// Error codes for unified error handling
namespace ErrorCode {
    constexpr const char* INVALID   = "invalid";    // Bad request, unknown command
    constexpr const char* NOT_FOUND = "not_found";  // App/file/var not found
    constexpr const char* OFFLINE   = "offline";    // No internet connection
    constexpr const char* TIMEOUT   = "timeout";    // Server didn't respond
    constexpr const char* SERVER    = "server";     // HTTP 5xx error
    constexpr const char* DENIED    = "denied";     // HTTP 401/403
    constexpr const char* BUSY      = "busy";       // Resource busy
    constexpr const char* MEMORY    = "memory";     // Out of RAM/PSRAM
}

/**
 * How the payload should be delivered by transport
 */
enum class ResponseType {
    Short,       // Inline in JSON (default — no extra payload)
    StringData,  // Large text — BLE: chunked text, Serial: print
    BinaryData   // Raw bytes — BLE: BinTransfer chunks, Serial: base64
};

/**
 * Command execution result
 */
struct Result {
    bool success = false;
    P::String status;
    P::String errorCode;    // Error code (e.g., "offline", "timeout")
    P::String errorMessage; // Human readable message
    int httpCode = 0;       // HTTP status code (optional, for fetch errors)
    JsonDocument data;
    
    // Payload delivery type
    ResponseType type = ResponseType::Short;
    
    // Payload buffer (for StringData and BinaryData)
    uint8_t* payload = nullptr;
    uint32_t payloadSize = 0;
    
    // Constructors
    static Result ok(const char* msg = "ok") {
        Result r;
        r.success = true;
        r.status = msg;
        return r;
    }
    
    static Result err(const char* code, const char* message, int http = 0) {
        Result r;
        r.success = false;
        r.errorCode = code;
        r.errorMessage = message;
        r.httpCode = http;
        return r;
    }
    
    // Convenience error constructors
    static Result errInvalid(const char* msg)  { return err(ErrorCode::INVALID, msg); }
    static Result errNotFound(const char* msg) { return err(ErrorCode::NOT_FOUND, msg); }
    static Result errOffline(const char* msg)  { return err(ErrorCode::OFFLINE, msg); }
    static Result errTimeout(const char* msg)  { return err(ErrorCode::TIMEOUT, msg); }
    static Result errServer(const char* msg, int http = 500)  { return err(ErrorCode::SERVER, msg, http); }
    static Result errDenied(const char* msg, int http = 403)  { return err(ErrorCode::DENIED, msg, http); }
    static Result errBusy(const char* msg)     { return err(ErrorCode::BUSY, msg); }
    static Result errMemory(const char* msg)   { return err(ErrorCode::MEMORY, msg); }
    
    // HTTP error with auto-detected code
    static Result fromHttp(int httpCode, const char* message) {
        if (httpCode >= 200 && httpCode < 300) {
            return ok();
        } else if (httpCode == 401 || httpCode == 403) {
            return err(ErrorCode::DENIED, message, httpCode);
        } else if (httpCode == 404) {
            return err(ErrorCode::NOT_FOUND, message, httpCode);
        } else if (httpCode >= 500) {
            return err(ErrorCode::SERVER, message, httpCode);
        } else if (httpCode == 408 || httpCode == 504) {
            return err(ErrorCode::TIMEOUT, message, httpCode);
        } else {
            return err(ErrorCode::SERVER, message, httpCode);
        }
    }
    
    // Attach binary output (BLE: chunked via BinTransfer, Serial: base64)
    Result& withBinaryData(uint8_t* buf, uint32_t size) {
        type = ResponseType::BinaryData;
        payload = buf;
        payloadSize = size;
        return *this;
    }
    
    // Attach large string output (BLE: chunked text, Serial: print)
    Result& withStringData(uint8_t* buf, uint32_t size) {
        type = ResponseType::StringData;
        payload = buf;
        payloadSize = size;
        return *this;
    }
    
    // Backward compat alias
    Result& withBinary(uint8_t* buf, uint32_t size) {
        return withBinaryData(buf, size);
    }
};

/**
 * Execute command
 * 
 * @param subsystem  "ui", "sys", "app", "log"
 * @param cmd        command name
 * @param args       JSON array of arguments
 * @return Result with status and optional binary
 */
Result exec(const char* subsystem, const char* cmd, JsonArray args);

/**
 * Execute command from text line
 * 
 * @param line  "subsystem cmd arg1 arg2 ..."
 * @return Result
 */
Result exec(const char* line);

} // namespace Console
