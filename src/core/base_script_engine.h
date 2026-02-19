#pragma once

#include "core/script_engine.h"
#include "utils/psram_alloc.h"

/**
 * 
 * Subclasses only need to implement:
 *   init(), execute(), call(), name(), shutdown()
 * 
 * (e.g. LuaEngine syncs Lua VM tables).
 */
class BaseScriptEngine : public IScriptEngine {
public:
    // --- State ---
    void setState(const char* key, const char* value) override;
    void setState(const char* key, int value) override;
    void setState(const char* key, bool value) override;
    void setStateSilent(const char* key, const char* value) override;
    
    const char* getStateString(const char* key) override;
    int getStateInt(const char* key) override;
    bool getStateBool(const char* key) override;
    
    // --- Config ---
    void setConfig(const char* key, const char* value) override;
    void setConfig(const char* key, int value) override;
    void setConfig(const char* key, bool value) override;
    const char* getConfig(const char* key) override;
    
    // --- Callback ---
    void onStateChange(StateCallback cb) override;

protected:
    StateCallback m_stateCallback;
    mutable P::String m_getBuffer;
    P::Map<P::String, P::String> m_configVars;
    
    /// Notify UI about state change
    void notifyState(const char* key, const char* value);
};
