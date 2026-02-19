/**
 * externals.cpp - Stubs ONLY for EXTERNAL dependencies
 */
#include "Arduino.h"
#include "Wire.h"
#include "lvgl.h"
#include "LittleFS.h"

// Arduino instances
SerialClass Serial;
TwoWire Wire;
TwoWire Wire1;
EspClass ESP;

// LVGL fonts
extern "C" const lv_font_t Ubuntu_16px = {16};
extern "C" const lv_font_t Ubuntu_32px = {32};
extern "C" const lv_font_t Ubuntu_48px = {48};
extern "C" const lv_font_t Ubuntu_72px = {72};

const lv_font_t lv_font_montserrat_14 = {14};
const lv_font_t lv_font_montserrat_16 = {16};

// LVGL classes
const lv_obj_class_t lv_label_class = {};
const lv_obj_class_t lv_textarea_class = {};
const lv_obj_class_t lv_btn_class = {};
const lv_obj_class_t lv_switch_class = {};
const lv_obj_class_t lv_slider_class = {};

// LittleFS
LittleFSClass LittleFS;

// lodepng
extern "C" unsigned lodepng_decode32(unsigned char**, unsigned*, unsigned*, const unsigned char*, size_t) {
    return 1;
}

// create_markdown
void create_markdown(const char*, const char*, const char*, lv_obj_t*) {}

// ArduinoJson
#include "ArduinoJson.h"
// JsonVariant::as<const char*> defined in ArduinoJson.h stub

// All core modules linked from .o files
#include "utils/psram_alloc.h"
