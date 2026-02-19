#pragma once

#include <cstdint>
#include <functional>

/**
 * IScriptEngine - Abstract interface for scripting languages
 * 
 * Implementations: LuaEngine, FakeEngine (for testing)
 */
class IScriptEngine {
public:
    virtual ~IScriptEngine() = default;
    
    // Initialize engine
    virtual bool init() = 0;
    
    // Load and execute script code
    virtual bool execute(const char* code) = 0;
    
    // Call a function by name
    virtual bool call(const char* func) = 0;
    
    // State variables (reactive - triggers UI update)
    virtual void setState(const char* key, const char* value) = 0;
    virtual void setState(const char* key, int value) = 0;
    virtual void setState(const char* key, bool value) = 0;
    
    // Set state without triggering callback (for widget -> lua sync)
    virtual void setStateSilent(const char* key, const char* value) = 0;
    
    virtual const char* getStateString(const char* key) = 0;
    virtual int getStateInt(const char* key) = 0;
    virtual bool getStateBool(const char* key) = 0;
    
    // Config variables (persistent)
    virtual void setConfig(const char* key, const char* value) = 0;
    virtual void setConfig(const char* key, int value) = 0;
    virtual void setConfig(const char* key, bool value) = 0;
    virtual const char* getConfig(const char* key) = 0;
    
    // Callback when state changes (for UI binding)
    using StateCallback = std::function<void(const char* key, const char* value)>;
    virtual void onStateChange(StateCallback cb) = 0;
    
    // Engine info
    virtual const char* name() const = 0;
    
    // Shutdown
    virtual void shutdown() = 0;
};
