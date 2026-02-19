/**
 * Device singleton — compile-time board selection.
 */

#include "device.h"

#if defined(BOARD_ESP4848S040)
    #include "boards/esp4848s040/esp4848s040.h"
    static Esp4848S040 s_device;

#elif defined(BOARD_ESP8048W550)
    #include "boards/esp8048w550/esp8048w550.h"
    static Esp8048W550 s_device;

#elif defined(BOARD_TWATCH_2020)
    #include "boards/twatch2020/twatch2020.h"
    static TWatch2020 s_device;

#else
    #error "No board defined! Add -DBOARD_XXX to build_flags in platformio.ini"
#endif

Device& Device::inst() {
    return s_device;
}