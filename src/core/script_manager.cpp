#include "core/script_manager.h"
#include "core/state_store.h"
#include "ui/ui_html.h"
#include "utils/task_queue.h"
#include "core/call_queue.h"
#include "utils/log_config.h"
#include <cstring>

static const char* TAG = "ScriptMgr";
static const char* VERSION = "1.7.0";

IScriptEngine* ScriptManager::s_engine = nullptr;

void ScriptManager::timer_callback(TimerHandle_t xTimer) {
    TimerContext* ctx = static_cast<TimerContext*>(pvTimerGetTimerID(xTimer));
    if (ctx && ctx->callback[0]) {
        CallQueue::push(ctx->callback);
    }
}

ScriptManager::~ScriptManager() {
    shutdown();
}

bool ScriptManager::init(IScriptEngine* engine) {
    if (!engine) {
        LOG_E(Log::LUA, "No engine provided");
        return false;
    }
    
    m_engine = engine;
    
    LOG_D(Log::LUA, "========================================");
    LOG_D(Log::LUA, "ScriptManager::init(%s) v%s", engine->name(), VERSION);
    LOG_D(Log::LUA, "========================================");
    
    // Init call queue and route calls to engine
    CallQueue::init();
    s_engine = engine;
    CallQueue::setHandler([](const P::String& funcName) {
        if (s_engine) {
            if (funcName.find('(') != P::String::npos) {
                s_engine->execute(funcName.c_str());
            } else {
                s_engine->call(funcName.c_str());
            }
        }
    });
    
    if (!m_engine->init()) {
        LOG_E(Log::LUA, "Engine init failed");
        return false;
    }
    
    loadState();
    ui_engine().syncWidgetValues();
    loadScript();
    setupTimers();
    setupOnclickHandler();
    setupOnTapHandler();
    setupOnHoldHandler();
    setupWidgetHandler();
    connectStateToUI();
    syncState();
    
    LOG_I(Log::LUA, "Init complete");
    return true;
}

void ScriptManager::shutdown() {
    for (auto& timer : m_timers) {
        xTimerStop(timer, 0);
        xTimerDelete(timer, 0);
    }
    m_timers.clear();
    
    for (auto ctx : m_timerContexts) {
        delete ctx;
    }
    m_timerContexts.clear();
    
    CallQueue::shutdown();
    s_engine = nullptr;
    
    if (m_engine) {
        m_engine->shutdown();
        m_engine = nullptr;
    }
}

void ScriptManager::loadState() {
    auto& ui = ui_engine();
    int count = ui.stateCount();
    
    LOG_D(Log::LUA, "Loading state (%d vars):", count);
    
    for (int i = 0; i < count; i++) {
        const char* name = ui.stateVarName(i);
        const char* vtype = ui.stateVarType(i);
        const char* def = ui.stateVarDefault(i);
        
        LOG_V(Log::LUA, "  state.%s : %s = '%s'", name, vtype, def ? def : "");
        
        if (!name) continue;
        
        if (strcmp(vtype, "bool") == 0) {
            bool val = def && (strcmp(def, "true") == 0 || strcmp(def, "1") == 0);
            m_engine->setState(name, val);
        } else if (strcmp(vtype, "int") == 0) {
            int val = def ? atoi(def) : 0;
            m_engine->setState(name, val);
        } else {
            m_engine->setState(name, def ? def : "");
        }
    }
}

void ScriptManager::syncState() {
    auto& ui = ui_engine();
    int count = ui.stateCount();
    
    LOG_D(Log::LUA, "Syncing state (%d vars) through engine", count);
    
    for (int i = 0; i < count; i++) {
        const char* name = ui.stateVarName(i);
        if (!name) continue;
        
        P::String val = State::store().getAsString(name);
        m_engine->setState(name, val.c_str());
    }
}

void ScriptManager::loadScript() {
    auto& ui = ui_engine();
    const char* code = ui.scriptCode();
    const char* lang = ui.scriptLang();
    
    if (!code || !code[0]) {
        LOG_W(Log::LUA, "No script code");
        return;
    }
    
    LOG_D(Log::LUA, "Loading script (lang='%s', %d bytes)", lang, (int)strlen(code));
    m_engine->execute(code);
}

void ScriptManager::setupTimers() {
    auto& ui = ui_engine();
    int count = ui.timerCount();
    
    LOG_D(Log::LUA, "Setting up %d timers:", count);
    
    if (count == 0) {
        LOG_W(Log::LUA, "No timers found in HTML!");
        return;
    }
    
    for (int i = 0; i < count; i++) {
        int interval = ui.timerInterval(i);
        const char* callback = ui.timerCallback(i);
        
        LOG_D(Log::LUA, "  [%d] interval=%d callback=%s", i, interval, callback ? callback : "NULL");
        
        if (interval <= 0 || !callback) {
            LOG_W(Log::LUA, "  Skipping invalid timer");
            continue;
        }
        
        TimerContext* ctx = new TimerContext();
        strncpy(ctx->callback, callback, sizeof(ctx->callback) - 1);
        ctx->callback[sizeof(ctx->callback) - 1] = '\0';
        m_timerContexts.push_back(ctx);
        
        TimerHandle_t timer = xTimerCreate(
            callback,
            pdMS_TO_TICKS(interval),
            pdTRUE,
            ctx,
            timer_callback
        );
        
        if (!timer) {
            LOG_E(Log::LUA, "Failed to create timer");
            continue;
        }
        
        LOG_D(Log::LUA, "  Timer created, starting...");
        
        if (xTimerStart(timer, 0) != pdPASS) {
            LOG_E(Log::LUA, "Failed to start timer");
            xTimerDelete(timer, 0);
            continue;
        }
        
        LOG_D(Log::LUA, "  Timer STARTED: %dms -> %s()", interval, callback);
        m_timers.push_back(timer);
    }
    
    LOG_D(Log::LUA, "Total timers running: %d", (int)m_timers.size());
}

void ScriptManager::setupOnclickHandler() {
    LOG_D(Log::LUA, "Setting up onclick handler (via queue)");
    
    ui_engine().setOnClickHandler([](const char* func_name) {
        if (func_name && func_name[0]) {
            CallQueue::push(func_name);
        }
    });
}

void ScriptManager::setupOnTapHandler() {
    LOG_D(Log::LUA, "Setting up ontap handler (with x,y args)");
    
    s_engine = m_engine;
    
    ui_engine().setOnTapHandler([](const char* func_name, int x, int y) {
        if (s_engine && func_name && func_name[0]) {
            char call_str[128];
            snprintf(call_str, sizeof(call_str), "%s(%d, %d)", func_name, x, y);
            s_engine->execute(call_str);
        }
    });
}

void ScriptManager::setupOnHoldHandler() {
    LOG_D(Log::LUA, "Setting up onhold handlers");
    
    s_engine = m_engine;
    
    ui_engine().setOnHoldHandler([](const char* func_name) {
        if (func_name && func_name[0]) {
            CallQueue::push(func_name);
        }
    });
    
    ui_engine().setOnHoldXYHandler([](const char* func_name, int x, int y) {
        if (s_engine && func_name && func_name[0]) {
            char call_str[128];
            snprintf(call_str, sizeof(call_str), "%s(%d, %d)", func_name, x, y);
            s_engine->execute(call_str);
        }
    });
}

void ScriptManager::setupWidgetHandler() {
    LOG_D(Log::LUA, "Setting up widget state change handler");
    
    s_engine = m_engine;
    
    ui_engine().setStateChangeHandler([](const char* var_name, const char* value) {
        if (s_engine && var_name) {
            s_engine->setStateSilent(var_name, value);
        }
    });
}

void ScriptManager::connectStateToUI() {
    LOG_D(Log::LUA, "Connecting state changes to UI via {bindings}");
    
    m_engine->onStateChange([](const char* key, const char* value) {
        UI::updateBinding(key, value);
    });
}
