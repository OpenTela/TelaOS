#pragma once
/**
 * YamlConfig — generic YAML-backed configuration.
 *
 * Built on top of Store (same VarType/VarValue as app state).
 * Keys use dot notation: "display.brightness", "power.auto_sleep".
 * File format: standard YAML with one level of nesting.
 *
 * Usage:
 *   YamlConfig cfg("/system/config.yml");
 *   cfg.define("display.brightness", VarType::Int, 255);
 *   cfg.define("power.auto_sleep", VarType::Bool, true);
 *   cfg.load();   // overrides defaults from file
 *   cfg.set("power.auto_sleep", false);  // auto-saves
 */

#include "state_store.h"

class YamlConfig {
public:
    explicit YamlConfig(const char* path, const char* tag = "YamlConfig");

    // Define a variable with type and default
    void define(const char* key, VarType type, const VarValue& defaultVal);

    // Load from file (call after all define()s)
    bool load();

    // Save current values to file
    bool save();

    // Typed access (key = "section.name", e.g. "display.brightness")
    P::String getString(const char* key) const { return m_store.getString(key); }
    int        getInt(const char* key)    const { return m_store.getInt(key); }
    bool       getBool(const char* key)   const { return m_store.getBool(key); }
    float      getFloat(const char* key)  const { return m_store.getFloat(key); }

    // Set + auto-save
    void set(const char* key, const VarValue& value);
    void set(const char* key, const char* value);
    void set(const char* key, int value)   { set(key, VarValue(value)); }
    void set(const char* key, bool value)  { set(key, VarValue(value)); }
    void set(const char* key, float value) { set(key, VarValue(value)); }

    // Raw store access
    const Store& store() const { return m_store; }

    // Debug
    void dump() const;

private:
    bool parseYaml(const char* content);
    P::String toYaml() const;

    Store m_store;
    P::String m_path;
};
