#pragma once

#include <map>
#include <variant>
#include <functional>
#include <cstring>
#include "utils/psram_alloc.h"
#include "utils/log_config.h"
#include "utils/log_config.h"

// ============ TYPES ============

enum class VarType { String, Int, Bool, Float };

using VarValue = std::variant<P::String, int, bool, float>;

struct Variable {
    VarType type = VarType::String;
    VarValue value;
    VarValue default_val;
};

// ============ STORE ============

class Store {
public:
    explicit Store(const char* tag = "Store") : m_tag(tag) {}
    
    // ============ DEFINE ============
    
    void define(const P::String& name, VarType type, const VarValue& default_val) {
        if (m_vars.find(name) == m_vars.end()) {
            m_names.push_back(name);
        }
        Variable var;
        var.type = type;
        var.value = default_val;
        var.default_val = default_val;
        m_vars[name] = var;
        if (Log::get(Log::STATE) >= Log::Debug) OS_LOGD("State", "Defined: %s", name.c_str());
    }
    
    void defineString(const P::String& name, const P::String& def = "") {
        define(name, VarType::String, def);
    }
    
    void defineInt(const P::String& name, int def = 0) {
        define(name, VarType::Int, def);
    }
    
    void defineBool(const P::String& name, bool def = false) {
        define(name, VarType::Bool, def);
    }
    
    void defineFloat(const P::String& name, float def = 0.0f) {
        define(name, VarType::Float, def);
    }
    
    // ============ GET ============
    
    bool has(const P::String& name) const {
        return m_vars.find(name) != m_vars.end();
    }
    
    const Variable* getVar(const P::String& name) const {
        auto it = m_vars.find(name);
        return it != m_vars.end() ? &it->second : nullptr;
    }
    
    VarType getType(const P::String& name) const {
        auto* var = getVar(name);
        return var ? var->type : VarType::String;
    }
    
    P::String getString(const P::String& name) const {
        auto* var = getVar(name);
        if (!var) return "";
        if (auto* s = std::get_if<P::String>(&var->value)) return *s;
        return getAsString(name);
    }
    
    int getInt(const P::String& name) const {
        auto* var = getVar(name);
        if (!var) return 0;
        if (auto* i = std::get_if<int>(&var->value)) return *i;
        if (auto* s = std::get_if<P::String>(&var->value)) return std::atoi(s->c_str());
        return 0;
    }
    
    bool getBool(const P::String& name) const {
        auto* var = getVar(name);
        if (!var) return false;
        if (auto* b = std::get_if<bool>(&var->value)) return *b;
        if (auto* s = std::get_if<P::String>(&var->value)) {
            return *s == "true" || *s == "1";
        }
        return false;
    }
    
    float getFloat(const P::String& name) const {
        auto* var = getVar(name);
        if (!var) return 0.0f;
        if (auto* f = std::get_if<float>(&var->value)) return *f;
        if (auto* s = std::get_if<P::String>(&var->value)) return std::atof(s->c_str());
        return 0.0f;
    }
    
    P::String getAsString(const P::String& name) const {
        auto* var = getVar(name);
        if (!var) return "";
        return valueToString(var->value);
    }
    
    // ============ SET ============
    
    void set(const P::String& name, const VarValue& value, bool notify = true) {
        auto it = m_vars.find(name);
        if (it == m_vars.end()) {
            Variable var;
            var.type = VarType::String;
            var.value = value;
            var.default_val = value;
            m_vars[name] = var;
            m_names.push_back(name);
            it = m_vars.find(name);
        }
        
        if (it->second.value == value) return;
        
        it->second.value = value;
        
        if (notify && m_onChange) {
            m_onChange(name, value);
        }
    }
    
    void setString(const P::String& name, const P::String& value, bool notify = true) {
        set(name, VarValue(value), notify);
    }
    
    // Set from string, converting to variable's declared type
    void setFromString(const P::String& name, const P::String& strValue, bool notify = true) {
        auto it = m_vars.find(name);
        if (it == m_vars.end()) {
            // Variable not defined, create as string
            set(name, VarValue(strValue), notify);
            return;
        }
        
        // Convert to declared type
        switch (it->second.type) {
            case VarType::String:
                set(name, VarValue(strValue), notify);
                break;
            case VarType::Int:
                set(name, VarValue(std::atoi(strValue.c_str())), notify);
                break;
            case VarType::Bool:
                set(name, VarValue(strValue == "true" || strValue == "1"), notify);
                break;
            case VarType::Float:
                set(name, VarValue((float)std::atof(strValue.c_str())), notify);
                break;
        }
    }
    
    void setInt(const P::String& name, int value, bool notify = true) {
        set(name, value, notify);
    }
    
    void setBool(const P::String& name, bool value, bool notify = true) {
        set(name, value, notify);
    }
    
    void setFloat(const P::String& name, float value, bool notify = true) {
        set(name, value, notify);
    }
    
    // ============ RESET / CLEAR ============
    
    void reset(const P::String& name) {
        auto it = m_vars.find(name);
        if (it != m_vars.end()) {
            it->second.value = it->second.default_val;
            if (m_onChange) {
                m_onChange(name, it->second.value);
            }
        }
    }
    
    void resetAll() {
        for (auto& [name, var] : m_vars) {
            var.value = var.default_val;
        }
    }
    
    void clear() {
        m_vars.clear();
        m_names.clear();
    }
    
    // ============ INDEX ACCESS ============
    
    size_t count() const { return m_vars.size(); }
    
    const P::String& nameAt(size_t idx) const {
        static const P::String empty;
        return idx < m_names.size() ? m_names[idx] : empty;
    }
    
    VarType typeAt(size_t idx) const {
        if (idx >= m_names.size()) return VarType::String;
        auto* var = getVar(m_names[idx]);
        return var ? var->type : VarType::String;
    }
    
    P::String defaultAt(size_t idx) const {
        if (idx >= m_names.size()) return "";
        auto* var = getVar(m_names[idx]);
        return var ? valueToString(var->default_val) : "";
    }
    
    // ============ ITERATION ============
    
    const P::Map<P::String, Variable>& variables() const { return m_vars; }
    
    // ============ CALLBACK ============
    
    void onChange(std::function<void(const P::String&, const VarValue&)> cb) {
        m_onChange = cb;
    }
    
    // ============ UTILS ============
    
    static const char* typeToString(VarType t) {
        switch (t) {
            case VarType::Bool: return "bool";
            case VarType::Int: return "int";
            case VarType::Float: return "float";
            default: return "string";
        }
    }
    
    static VarType stringToType(const char* s) {
        if (strcmp(s, "bool") == 0) return VarType::Bool;
        if (strcmp(s, "int") == 0) return VarType::Int;
        if (strcmp(s, "float") == 0) return VarType::Float;
        return VarType::String;
    }
    
    static bool isKnownType(const char* s) {
        return strcmp(s, "bool") == 0 || strcmp(s, "int") == 0 || 
               strcmp(s, "float") == 0 || strcmp(s, "string") == 0;
    }
    
    void dump() const {
        if (Log::get(Log::STATE) >= Log::Debug) OS_LOGD("State", "=== %s (%d vars) ===", m_tag, (int)m_vars.size());
        for (const auto& name : m_names) {
            auto* var = getVar(name);
            if (var) {
                if (Log::get(Log::STATE) >= Log::Verbose) OS_LOGV("State", "  %s : %s = %s", 
                    name.c_str(), 
                    typeToString(var->type),
                    valueToString(var->value).c_str());
            }
        }
    }

private:
    const char* m_tag;
    P::Map<P::String, Variable> m_vars;
    P::Array<P::String> m_names;
    std::function<void(const P::String&, const VarValue&)> m_onChange;
    
    static P::String valueToString(const VarValue& v) {
        return std::visit([](auto&& val) -> P::String {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, P::String>) {
                return val;
            } else if constexpr (std::is_same_v<T, int>) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%d", val);
                return buf;
            } else if constexpr (std::is_same_v<T, bool>) {
                return val ? "true" : "false";
            } else if constexpr (std::is_same_v<T, float>) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2f", val);
                return buf;
            }
            return "";
        }, v);
    }
};

// ============ GLOBAL INSTANCES ============

namespace State {
    inline Store& store() {
        static Store s("State");
        return s;
    }
}

// ============ C API ============

const char* state_store_get(const char* key);
void state_store_set(const char* key, const char* value);
void state_store_set_silent(const char* key, const char* value);
