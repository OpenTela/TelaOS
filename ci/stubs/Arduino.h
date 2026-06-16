#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <string>
#include "freertos/FreeRTOS.h"
#include "esp_system.h"

// Forward declarations
class String;

inline uint32_t millis() { return 0; }
inline uint32_t micros() { return 0; }
inline void delay(uint32_t) {}

class SerialClass {
private:
    std::string _inputBuffer;
    std::string _outputBuffer;
    size_t _readPos = 0;
    
public:
    void begin(int) {}
    
    // === Input (mock commands) ===
    void setInput(const char* cmd) {
        _inputBuffer = cmd ? cmd : "";
        _readPos = 0;
    }
    
    void appendInput(const char* cmd) {
        _inputBuffer += cmd ? cmd : "";
    }
    
    int available() { 
        return (int)(_inputBuffer.size() - _readPos); 
    }
    
    int read() { 
        if (_readPos >= _inputBuffer.size()) return -1;
        return (unsigned char)_inputBuffer[_readPos++];
    }
    
    int peek() {
        if (_readPos >= _inputBuffer.size()) return -1;
        return (unsigned char)_inputBuffer[_readPos];
    }
    
    size_t readBytes(char* buf, size_t len) {
        size_t i = 0;
        while (i < len && _readPos < _inputBuffer.size()) {
            buf[i++] = _inputBuffer[_readPos++];
        }
        return i;
    }
    
    // === Output (capture response) ===
    void print(const char* s) { if (s) _outputBuffer += s; }
    void print(char c) { _outputBuffer += c; }
    void print(int v) { _outputBuffer += std::to_string(v); }
    void print(unsigned int v) { _outputBuffer += std::to_string(v); }
    void print(long v) { _outputBuffer += std::to_string(v); }
    void print(unsigned long v) { _outputBuffer += std::to_string(v); }
    void print(double v) { _outputBuffer += std::to_string(v); }
    
    void println(const char* s) { print(s); _outputBuffer += "\n"; }
    void println(const String& s);
    void println() { _outputBuffer += "\n"; }
    void println(int v) { print(v); _outputBuffer += "\n"; }
    void println(unsigned int v) { print(v); _outputBuffer += "\n"; }
    void println(long v) { print(v); _outputBuffer += "\n"; }
    void println(unsigned long v) { print(v); _outputBuffer += "\n"; }
    
    void printf(const char* fmt, ...) {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        _outputBuffer += buf;
    }
    
    size_t write(uint8_t c) { _outputBuffer += (char)c; return 1; }
    size_t write(const uint8_t* buf, size_t len) {
        _outputBuffer.append((const char*)buf, len);
        return len;
    }
    
    void flush() {}
    
    // === Test helpers ===
    std::string getOutput() const { return _outputBuffer; }
    void clearOutput() { _outputBuffer.clear(); }
    void clearInput() { _inputBuffer.clear(); _readPos = 0; }
    void clear() { clearInput(); clearOutput(); }
};
extern SerialClass Serial;

#define ESP_LOGI(tag, fmt, ...) do{}while(0)
#define ESP_LOGW(tag, fmt, ...) do{}while(0)
#define ESP_LOGE(tag, fmt, ...) do{}while(0)
#define ESP_LOGD(tag, fmt, ...) do{}while(0)
#define ESP_LOGV(tag, fmt, ...) do{}while(0)

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }

// Arduino String class
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
};

// Deferred definition
inline void SerialClass::println(const String& s) {}

#define IRAM_ATTR
