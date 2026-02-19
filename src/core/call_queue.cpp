#include "core/call_queue.h"
#include "utils/log_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <queue>
#include <set>

namespace CallQueue {

static const char* TAG = "CallQueue";
static constexpr size_t MAX_QUEUE_SIZE = 16;

static Handler g_handler = nullptr;
static SemaphoreHandle_t g_mutex = nullptr;
static std::queue<P::String> g_queue;
static std::set<P::String> g_pending;
static bool g_initialized = false;
static bool g_processing = false;
static uint32_t g_dropped = 0;

void init() {
    if (g_initialized) return;
    g_mutex = xSemaphoreCreateMutex();
    g_initialized = true;
    g_dropped = 0;
    LOG_I(Log::APP, "CallQueue initialized (max: %d)", (int)MAX_QUEUE_SIZE);
}

void shutdown() {
    if (g_mutex) {
        xSemaphoreTake(g_mutex, portMAX_DELAY);
        std::queue<P::String> empty;
        g_queue.swap(empty);
        g_pending.clear();
        xSemaphoreGive(g_mutex);
        
        vSemaphoreDelete(g_mutex);
        g_mutex = nullptr;
    }
    g_handler = nullptr;
    g_initialized = false;
    g_processing = false;
    
    if (g_dropped > 0) {
        LOG_W(Log::APP, "CallQueue shutdown: %lu calls dropped", (unsigned long)g_dropped);
    }
    g_dropped = 0;
}

void setHandler(Handler handler) {
    g_handler = handler;
}

void push(const P::String& funcName) {
    if (!g_mutex || funcName.empty()) return;
    
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    
    if (g_queue.size() >= MAX_QUEUE_SIZE) {
        g_dropped++;
        LOG_E(Log::APP, "Queue overflow! Dropping: %s (size: %d, dropped: %lu)", 
                 funcName.c_str(), (int)g_queue.size(), (unsigned long)g_dropped);
        xSemaphoreGive(g_mutex);
        return;
    }
    
    if (g_pending.count(funcName) > 0) {
        LOG_D(Log::APP, "Dedupe: %s already pending", funcName.c_str());
        xSemaphoreGive(g_mutex);
        return;
    }
    
    g_queue.push(funcName);
    g_pending.insert(funcName);
    LOG_D(Log::APP, "Queued: %s (size: %d)", funcName.c_str(), (int)g_queue.size());
    
    xSemaphoreGive(g_mutex);
}

void process() {
    if (!g_mutex || !g_handler) return;
    if (g_processing) return;
    g_processing = true;
    
    std::queue<P::String> calls;
    
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    calls.swap(g_queue);
    g_pending.clear();
    xSemaphoreGive(g_mutex);
    
    while (!calls.empty()) {
        P::String funcName = calls.front();
        calls.pop();
        
        LOG_D(Log::APP, "Exec: %s", funcName.c_str());
        g_handler(funcName);
    }
    
    g_processing = false;
}

bool hasPending() {
    if (!g_mutex) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    bool has = !g_queue.empty();
    xSemaphoreGive(g_mutex);
    return has;
}

size_t size() {
    if (!g_mutex) return 0;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    size_t sz = g_queue.size();
    xSemaphoreGive(g_mutex);
    return sz;
}

uint32_t droppedCount() {
    return g_dropped;
}

} // namespace CallQueue
