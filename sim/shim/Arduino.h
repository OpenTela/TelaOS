// Arduino.h — simulator version of the Arduino API used by TelaOS.
//
// Same surface as TelaCI's stub (ci/stubs/Arduino.h), with three differences:
//  - millis()/delay() run on the simulator's virtual clock (deterministic, can be paced)
//  - Serial prints to stdout
//  - Serial input is fed by the robot: "> app launch hello" types a console line
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"

class String;

uint32_t sim_millis();        // virtual clock, sim_tela_shim.cpp
void sim_delay(uint32_t ms);  // advances the virtual clock, paces the window

inline uint32_t millis() { return sim_millis(); }
inline uint32_t micros() { return sim_millis() * 1000u; }
inline void delay(uint32_t ms) { sim_delay(ms); }
inline void yield() {}

class SerialClass {
    std::string in_;
    size_t pos_ = 0;

public:
    void begin(int) {}
    operator bool() const { return true; }

    // Input: the robot types console lines here
    void appendInput(const char* s) {
        if (s) in_ += s;
    }
    int available() { return static_cast<int>(in_.size() - pos_); }
    int read() {
        if (pos_ >= in_.size()) return -1;
        const int c = static_cast<unsigned char>(in_[pos_++]);
        if (pos_ == in_.size()) {
            in_.clear();
            pos_ = 0;
        }
        return c;
    }
    int peek() { return pos_ < in_.size() ? static_cast<unsigned char>(in_[pos_]) : -1; }
    size_t readBytes(char* buf, size_t len) {
        size_t i = 0;
        while (i < len && available() > 0) buf[i++] = static_cast<char>(read());
        return i;
    }

    // Output: straight to stdout
    void print(const char* s) {
        if (s) std::fputs(s, stdout);
    }
    void print(char c) { std::fputc(c, stdout); }
    void print(int v) { std::printf("%d", v); }
    void print(unsigned int v) { std::printf("%u", v); }
    void print(long v) { std::printf("%ld", v); }
    void print(unsigned long v) { std::printf("%lu", v); }
    void print(double v) { std::printf("%.2f", v); }
    void print(const String& s);
    void println() { std::fputc('\n', stdout); }
    void println(const char* s) {
        print(s);
        println();
    }
    void println(const String& s);
    void println(int v) {
        print(v);
        println();
    }
    void println(unsigned int v) {
        print(v);
        println();
    }
    void println(long v) {
        print(v);
        println();
    }
    void println(unsigned long v) {
        print(v);
        println();
    }
    void printf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::vprintf(fmt, args);
        va_end(args);
    }
    size_t write(uint8_t c) {
        std::fputc(c, stdout);
        return 1;
    }
    size_t write(const uint8_t* buf, size_t len) { return std::fwrite(buf, 1, len, stdout); }
    void flush() { std::fflush(stdout); }
};
extern SerialClass Serial;

#define ESP_LOGI(tag, fmt, ...) do {} while (0)
#define ESP_LOGW(tag, fmt, ...) do {} while (0)
#define ESP_LOGE(tag, fmt, ...) do {} while (0)
#define ESP_LOGD(tag, fmt, ...) do {} while (0)
#define ESP_LOGV(tag, fmt, ...) do {} while (0)

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define FALLING 2
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}

// Arduino String (as in TelaCI)
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const char* s) : std::string(s ? s : "") {}
    String(const std::string& s) : std::string(s) {}
};

inline void SerialClass::print(const String& s) { print(s.c_str()); }
inline void SerialClass::println(const String& s) { println(s.c_str()); }

#define IRAM_ATTR
