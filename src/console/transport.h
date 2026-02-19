/**
 * Transport - Abstract interface for console I/O
 * 
 * Implementations: SerialTransport, BleTransport
 */
#pragma once

#include "console/console.h"

namespace Console {

/**
 * Abstract transport interface
 */
class Transport {
public:
    virtual ~Transport() = default;
    
    /**
     * Send command result
     * @param requestId  request ID for response correlation
     * @param result     execution result (may contain binary)
     */
    virtual void sendResult(int requestId, const Result& result) = 0;
    
    /**
     * Send raw text (for Serial CLI)
     */
    virtual void sendText(const char* text) = 0;
    
    /**
     * Check if transport supports binary transfer
     */
    virtual bool supportsBinary() const = 0;
    
    /**
     * Check if transport is connected
     */
    virtual bool isConnected() const = 0;
    
    /**
     * Transport name for logging
     */
    virtual const char* name() const = 0;
};

} // namespace Console
