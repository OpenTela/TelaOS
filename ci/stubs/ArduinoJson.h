#pragma once
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

namespace ArduinoJson {
    struct Allocator {
        virtual void* allocate(size_t) = 0;
        virtual void deallocate(void*) = 0;
        virtual void* reallocate(void*, size_t) = 0;
        virtual ~Allocator() = default;
    };
}

// Forward declarations
class JsonDocument;
class JsonObject;
class JsonArray;
class JsonObjectConst;
class JsonArrayConst;

class JsonPairConst { 
public: 
    std::string key() const { return ""; } 
    class JsonVariantConst value() const; 
};

class JsonVariant {
public:
    std::string str_val;
    template<typename T> bool is() const { return false; }
    template<typename T> T as() const;
    template<typename T> T to();
    operator bool() const { return !str_val.empty(); }
    operator const char*() const { return str_val.c_str(); }
    JsonVariant operator[](const char*) { return {}; }
    JsonVariant operator[](const std::string& s) { return operator[](s.c_str()); }
    JsonVariant operator[](int) { return {}; }
    const char* c_str() const { return str_val.c_str(); }
    bool isNull() const { return str_val.empty(); }
    JsonVariant& operator=(const char* s) { str_val = s ? s : ""; return *this; }
    JsonVariant& operator=(const std::string& s) { str_val = s; return *this; }
    JsonVariant& operator=(int v) { str_val = std::to_string(v); return *this; }
    JsonVariant& operator=(unsigned int v) { str_val = std::to_string(v); return *this; }
    JsonVariant& operator=(long v) { str_val = std::to_string(v); return *this; }
    JsonVariant& operator=(unsigned long v) { str_val = std::to_string(v); return *this; }
    JsonVariant& operator=(bool v) { str_val = v ? "true" : "false"; return *this; }
    JsonVariant& operator=(double) { return *this; }
    JsonVariant& operator=(const JsonVariantConst&) { return *this; }
    JsonObject createNestedObject();
    void add(const char*) {}
    void add(int) {}
    void add(const JsonVariant&) {}
    void add(const JsonDocument&) {}
    int operator|(int def) const { return def; }
    const char* operator|(const char* def) const { return def ? def : ""; }
    JsonVariant operator|(JsonVariant other) const { return other; }
};

// Specialization for is<const char*>
template<> inline bool JsonVariant::is<const char*>() const { return !str_val.empty(); }

class JsonVariantConst {
public:
    bool isNull() const { return true; }
    template<typename T> bool is() const { return false; }
    template<typename T> T as() const { return T(); }
    size_t size() const { return 0; }
    JsonVariantConst operator[](int) const { return {}; }
    JsonVariantConst operator[](const char*) const { return {}; }
};

inline JsonVariantConst JsonPairConst::value() const { return {}; }

class JsonObjectConst {
public:
    JsonVariantConst operator[](const char*) const { return {}; }
    JsonPairConst* begin() { return nullptr; }
    JsonPairConst* end() { return nullptr; }
};

class JsonArrayConst {
public:
    size_t size() const { return 0; }
    JsonVariantConst operator[](int) const { return {}; }
    JsonVariantConst* begin() { return nullptr; }
    JsonVariantConst* end() { return nullptr; }
};

class JsonArray {
    std::vector<std::string> items;
public:
    JsonArray() = default;
    JsonArray(const JsonArray& other) : items(other.items) {}
    JsonArray& operator=(const JsonArray& other) { items = other.items; return *this; }
    
    size_t size() const { return items.size(); }
    JsonVariant operator[](int i) { 
        if (i >= 0 && i < (int)items.size()) {
            JsonVariant v;
            v.str_val = items[i];
            return v;
        }
        return {}; 
    }
    JsonVariant operator[](int i) const { 
        if (i >= 0 && i < (int)items.size()) {
            JsonVariant v;
            v.str_val = items[i];
            return v;
        }
        return {}; 
    }
    void add(const char* s) { items.push_back(s ? s : ""); }
    void add(int v) { items.push_back(std::to_string(v)); }
    void add(const JsonVariant& v) { items.push_back(v.str_val); }
    void add(const JsonDocument&) {}
    void add(const std::string& s) { items.push_back(s); }
    JsonVariant* begin() { return nullptr; }
    JsonVariant* end() { return nullptr; }
    JsonObject createNestedObject();
    template<typename T> T add();
};

class JsonObject {
public:
    JsonVariant operator[](const char*) { return {}; }
    JsonVariant operator[](const std::string& s) { return operator[](s.c_str()); }
    JsonPairConst* begin() { return nullptr; }
    JsonPairConst* end() { return nullptr; }
};

template<> inline JsonArray JsonVariant::to<JsonArray>() { return JsonArray(); }
template<> inline JsonObject JsonVariant::to<JsonObject>() { return JsonObject(); }
template<> inline JsonArray JsonVariant::as<JsonArray>() const { return JsonArray(); }
template<> inline JsonObject JsonVariant::as<JsonObject>() const { return JsonObject(); }
// Rotating buffer for as<const char*> to avoid dangling pointer
static thread_local std::string _jsonVariantStrBufs[8];
static thread_local int _jsonVariantStrBufIdx = 0;
template<> inline const char* JsonVariant::as<const char*>() const { 
    _jsonVariantStrBufIdx = (_jsonVariantStrBufIdx + 1) % 8;
    _jsonVariantStrBufs[_jsonVariantStrBufIdx] = str_val;
    return _jsonVariantStrBufs[_jsonVariantStrBufIdx].c_str(); 
}
template<> inline int JsonVariant::as<int>() const { return str_val.empty() ? 0 : std::stoi(str_val); }
template<> inline float JsonVariant::as<float>() const { return str_val.empty() ? 0.0f : std::stof(str_val); }
template<> inline double JsonVariant::as<double>() const { return str_val.empty() ? 0.0 : std::stod(str_val); }
template<> inline bool JsonVariant::as<bool>() const { return str_val == "true" || str_val == "1"; }
template<> inline JsonObjectConst JsonVariantConst::as<JsonObjectConst>() const { return {}; }
template<> inline JsonArrayConst JsonVariantConst::as<JsonArrayConst>() const { return {}; }
inline JsonObject JsonVariant::createNestedObject() { return JsonObject(); }
inline JsonObject JsonArray::createNestedObject() { return JsonObject(); }
template<typename T> T JsonArray::add() { return T(); }
template<> inline JsonObject JsonArray::add<JsonObject>() { return JsonObject(); }

class JsonDocument {
public:
    JsonDocument() = default;
    JsonDocument(ArduinoJson::Allocator*) {}
    JsonVariant operator[](const char*) { return {}; }
    JsonVariant operator[](const std::string& s) { return operator[](s.c_str()); }
    JsonVariantConst operator[](const char*) const { return {}; }
    bool containsKey(const char*) const { return false; }
    bool isNull() const { return false; }
    size_t size() const { return 0; }
    bool is(const char*) const { return false; }
    template<typename T> bool is() const { return false; }
    template<typename T> T as() { return T(); }
    template<typename T> T as() const { return T(); }
    template<typename T> T to() { return T(); }
    JsonArray createNestedArray(const char*) { return {}; }
};

template<> inline JsonArray JsonDocument::to<JsonArray>() { return JsonArray(); }

using DynamicJsonDocument = JsonDocument;
struct DeserializationError {
    int code;
    DeserializationError(int c = 0) : code(c) {}
    operator bool() const { return code != 0; }
    const char* c_str() const { return code ? "error" : "ok"; }
};
inline const DeserializationError DeserializationOk{0};
inline DeserializationError deserializeJson(JsonDocument&, const char*) { return {0}; }
inline DeserializationError deserializeJson(JsonDocument&, const std::string&) { return {0}; }
inline size_t serializeJson(const JsonDocument&, char*, size_t) { return 0; }
template<typename S> inline size_t serializeJson(const JsonDocument&, S&) { return 0; }
template<typename S> inline size_t serializeJson(const JsonVariantConst&, S&) { return 0; }

// JsonPair for iteration
class JsonPair {
public:
    const char* key() const { return ""; }
    JsonVariant value() const { return {}; }
};

// serializeJson
inline size_t serializeJson(const JsonDocument&, std::string&) { return 0; }
