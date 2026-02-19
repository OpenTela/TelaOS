#include "core/state_store.h"
#include <cstdlib>

// Static buffer for C API
static thread_local P::String s_get_buffer;

const char* state_store_get(const char* key) {
    if (!key) return "";
    s_get_buffer = State::store().getAsString(key);
    return s_get_buffer.c_str();
}

void state_store_set(const char* key, const char* value) {
    if (!key) return;
    auto& store = State::store();
    
    auto type = store.getType(key);
    switch (type) {
        case VarType::Int:
            store.setInt(key, std::atoi(value ? value : "0"), true);
            break;
        case VarType::Bool:
            store.setBool(key, value && (strcmp(value, "true") == 0 || strcmp(value, "1") == 0), true);
            break;
        case VarType::Float:
            store.setFloat(key, std::atof(value ? value : "0"), true);
            break;
        default:
            store.setString(key, value ? value : "", true);
            break;
    }
}

void state_store_set_silent(const char* key, const char* value) {
    if (!key) return;
    auto& store = State::store();
    
    auto type = store.getType(key);
    switch (type) {
        case VarType::Int:
            store.setInt(key, std::atoi(value ? value : "0"), false);
            break;
        case VarType::Bool:
            store.setBool(key, value && (strcmp(value, "true") == 0 || strcmp(value, "1") == 0), false);
            break;
        case VarType::Float:
            store.setFloat(key, std::atof(value ? value : "0"), false);
            break;
        default:
            store.setString(key, value ? value : "", false);
            break;
    }
}
