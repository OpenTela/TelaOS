// sim_timers.cpp — FreeRTOS software timers on the virtual clock (shim/freertos/timers.h).
#include "freertos/timers.h"

#include <algorithm>
#include <vector>

uint32_t sim_millis();  // the virtual clock, sim_tela_shim.cpp

struct SimTimer {
    uint32_t period;
    bool reload;
    void* id;
    TimerCallbackFunction_t callback;
    bool active = false;
    bool deleted = false;
    uint32_t due = 0;
};

namespace {

// Created by the firmware, so in the watch's heap, as FreeRTOS keeps them in its own
std::vector<SimTimer*>& timers() {
    static std::vector<SimTimer*> list;
    return list;
}

bool live(TimerHandle_t t) { return t && !t->deleted; }

void purge() {
    auto& list = timers();
    list.erase(std::remove_if(list.begin(), list.end(), [](SimTimer* t) {
                   if (!t->deleted) return false;
                   delete t;
                   return true;
               }),
               list.end());
}

bool running = false;  // inside run(): a callback may delete timers, purged after it

}  // namespace

TimerHandle_t xTimerCreate(const char*, uint32_t period, int autoReload, void* id,
                           TimerCallbackFunction_t callback) {
    if (period == 0 || !callback) return nullptr;  // FreeRTOS asserts the same
    SimTimer* t = new SimTimer{period, autoReload != pdFALSE, id, callback};
    timers().push_back(t);
    return t;
}

int xTimerStart(TimerHandle_t t, uint32_t) {
    if (!live(t)) return pdFAIL;
    t->active = true;
    t->due = sim_millis() + t->period;
    return pdPASS;
}

int xTimerReset(TimerHandle_t t, uint32_t wait) { return xTimerStart(t, wait); }

int xTimerStop(TimerHandle_t t, uint32_t) {
    if (!live(t)) return pdFAIL;
    t->active = false;
    return pdPASS;
}

int xTimerChangePeriod(TimerHandle_t t, uint32_t period, uint32_t wait) {
    if (!live(t) || period == 0) return pdFAIL;
    t->period = period;
    return xTimerStart(t, wait);
}

int xTimerDelete(TimerHandle_t t, uint32_t) {
    if (!live(t)) return pdFAIL;
    t->active = false;
    t->deleted = true;
    if (!running) purge();
    return pdPASS;
}

int xTimerIsTimerActive(TimerHandle_t t) { return live(t) && t->active ? pdTRUE : pdFALSE; }

void* pvTimerGetTimerID(TimerHandle_t t) { return t ? t->id : nullptr; }

namespace SimTimers {

void run(uint32_t now) {
    if (running) return;  // a callback that waits: what is due fires after it
    running = true;
    for (;;) {
        SimTimer* next = nullptr;
        for (SimTimer* t : timers()) {
            if (t->active && !t->deleted && static_cast<int32_t>(now - t->due) >= 0 &&
                (!next || static_cast<int32_t>(t->due - next->due) < 0)) {
                next = t;
            }
        }
        if (!next) break;
        if (next->reload) next->due += next->period;
        else next->active = false;
        next->callback(next);
    }
    running = false;
    purge();
}

size_t count() {
    return static_cast<size_t>(
        std::count_if(timers().begin(), timers().end(), [](const SimTimer* t) { return !t->deleted; }));
}

}  // namespace SimTimers
