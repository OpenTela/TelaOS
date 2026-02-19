#include "core/base_script_engine.h"
#include "core/state_store.h"
#include "utils/log_config.h"
#include <cstring>

static const char* TAG = "ScriptEngine";

// --- State ---

void BaseScriptEngine::setState(const char* key, const char* value) {
    state_store_set(key, value);
    notifyState(key, value);
}

void BaseScriptEngine::setState(const char* key, int value) {
    State::store().setInt(key, value, false);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    notifyState(key, buf);
}

void BaseScriptEngine::setState(const char* key, bool value) {
    State::store().setBool(key, value, false);
    notifyState(key, value ? "true" : "false");
}

void BaseScriptEngine::setStateSilent(const char* key, const char* value) {
    state_store_set_silent(key, value);
}

const char* BaseScriptEngine::getStateString(const char* key) {
    m_getBuffer = State::store().getAsString(key);
    return m_getBuffer.c_str();
}

int BaseScriptEngine::getStateInt(const char* key) {
    return State::store().getInt(key);
}

bool BaseScriptEngine::getStateBool(const char* key) {
    return State::store().getBool(key);
}

// --- Config ---

void BaseScriptEngine::setConfig(const char* key, const char* value) {
    LOG_D(Log::LUA, "config.%s = '%s'", key, value ? value : "");
    m_configVars[key] = value ? value : "";
}

void BaseScriptEngine::setConfig(const char* key, int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    setConfig(key, buf);
}

void BaseScriptEngine::setConfig(const char* key, bool value) {
    setConfig(key, value ? "true" : "false");
}

const char* BaseScriptEngine::getConfig(const char* key) {
    auto it = m_configVars.find(key);
    return (it != m_configVars.end()) ? it->second.c_str() : "";
}

// --- Callback ---

void BaseScriptEngine::onStateChange(StateCallback cb) {
    m_stateCallback = cb;
}

void BaseScriptEngine::notifyState(const char* key, const char* value) {
    if (m_stateCallback) {
        m_stateCallback(key, value);
    }
}
