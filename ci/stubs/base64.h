#pragma once
#include <Arduino.h>

namespace base64 {
    inline String encode(const uint8_t* data, size_t len) {
        return String("[base64 data]");
    }
    inline size_t decode(const String& input, uint8_t* output, size_t maxLen) {
        return 0;
    }
}
