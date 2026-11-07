#pragma once
// driver/gpio.h — TelaCI stub: pin levels the tests set, and the pins asked to wake the chip.
#include <array>
#include <cstdint>

typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif

typedef int gpio_num_t;
typedef enum {
    GPIO_INTR_DISABLE,
    GPIO_INTR_POSEDGE,
    GPIO_INTR_NEGEDGE,
    GPIO_INTR_ANYEDGE,
    GPIO_INTR_LOW_LEVEL,
    GPIO_INTR_HIGH_LEVEL,
} gpio_int_type_t;

struct GpioMock {
    std::array<int, 49> level;                 // what gpio_get_level() reads: high unless a test says
    std::array<gpio_int_type_t, 49> wakeup{};  // gpio_wakeup_enable()/disable()
    GpioMock() { level.fill(1); }
};
inline GpioMock g_gpio_mock;

inline int gpio_get_level(gpio_num_t pin) {
    return pin >= 0 && pin < 49 ? g_gpio_mock.level[pin] : 0;
}
inline esp_err_t gpio_wakeup_enable(gpio_num_t pin, gpio_int_type_t type) {
    if (pin >= 0 && pin < 49) g_gpio_mock.wakeup[pin] = type;
    return ESP_OK;
}
inline esp_err_t gpio_wakeup_disable(gpio_num_t pin) {
    if (pin >= 0 && pin < 49) g_gpio_mock.wakeup[pin] = GPIO_INTR_DISABLE;
    return ESP_OK;
}
