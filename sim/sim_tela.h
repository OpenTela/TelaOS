// sim_tela.h — simulator services for the TelaOS firmware build.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace SimTela {

uint32_t now();                                         // virtual ms since power-on
void setDelayHook(std::function<void(uint32_t ms)> h);  // runs inside every delay()
int flashData(const std::string& data_dir);             // data/ → LittleFS, like `pio uploadfs`

}  // namespace SimTela
