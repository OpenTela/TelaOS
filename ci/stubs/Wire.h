#pragma once
#include <cstdint>

class TwoWire {
public:
    void begin(int sda = -1, int scl = -1, uint32_t freq = 100000) {}
    void beginTransmission(uint8_t) {}
    void write(uint8_t) {}
    uint8_t endTransmission(bool stop = true) { return 0; }
    uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
    int read() { return 0; }
    int available() { return 0; }
};
extern TwoWire Wire;
extern TwoWire Wire1;
