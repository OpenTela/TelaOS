#pragma once

#include "core/script_engine.h"
#include "core/call_queue.h"
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <vector>
#include <string>

/**
 * ScriptManager - Bridge between HTML parser and ScriptEngine
 */
class ScriptManager {
private:
    IScriptEngine* m_engine = nullptr;
    std::vector<TimerHandle_t> m_timers;
    
    struct TimerContext {
        char callback[32];
    };
    std::vector<TimerContext*> m_timerContexts;
    
    static IScriptEngine* s_engine;
    static void timer_callback(TimerHandle_t xTimer);
    
public:
    ~ScriptManager();
    
    bool init(IScriptEngine* engine);
    void shutdown();
    IScriptEngine* engine() { return m_engine; }
    
private:
    void loadState();
    void syncState();
    void loadScript();
    void setupTimers();
    void setupOnclickHandler();
    void setupOnTapHandler();
    void setupOnHoldHandler();
    void setupWidgetHandler();
    void connectStateToUI();
};
