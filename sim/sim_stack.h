// sim_stack.h — the watch's stack: the firmware runs on a stack of its own, 4/5 of the watch's loop task.
//
// On the watch all of TelaOS runs in Arduino's loop task: setup() and loop(), and in them the
// console, Lua, LVGL and the building of pages. Its stack is 8 KB (CONFIG_ARDUINO_LOOP_STACK_SIZE
// of Arduino-ESP32 2.0.17), or what the firmware sets (SET_LOOP_TASK_STACK_SIZE). A deeper call
// reboots the watch: "Stack canary watchpoint triggered (loopTask)".
//
// The simulator used to run the firmware on the PC's stack of megabytes and never saw that. Now
// the firmware runs on a stack of its own with nothing mapped below it: a deeper call stops the
// simulator right there and prints the calls that went so deep, with their frames.
//
// That stack is 4/5 of the watch's. The PC's frames are not the watch's (x86-64 GCC -O3 here,
// Xtensa GCC 8.4 -Os there): TelaOS's own are smaller on the watch (parse_children 368 bytes here,
// 304 there), LVGL's larger: it draws an object nested in another inside its parent's call, 464
// bytes a level here, 560 there. What fits in 4/5 here fits on the watch. --stack <bytes> gives
// another size: the watch's own, to find where it would stop.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace SimStack {

// Before the first run(): `bytes` of stack for the firmware, the watch's loop task has `watch`.
// 0 bytes: no stack of its own, the firmware runs on the PC's (ASan builds)
void init(size_t bytes, size_t watch);

// job on the firmware's stack. Called from the firmware's own stack, it just calls job
void run(const std::function<void()>& job);
// From the firmware's stack: job on the PC's stack (the simulator's window), then back
void onHost(const std::function<void()>& job);

size_t size();  // the firmware's stack, bytes; 0 when it has none of its own
size_t used();  // the most the firmware has used of it since the start
size_t watch();  // the watch's loop task
std::string json();  // {"size": …, "used": …, "watch": …}

}  // namespace SimStack
