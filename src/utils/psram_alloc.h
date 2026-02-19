#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <new>
#include <cstdlib>
#include <string_view>
#include "esp_heap_caps.h"

// ============ M:: DRAM (Memory) ============
// For statics, created before psramInit()

namespace M {

template <typename T>
struct Allocator {
    using value_type = T;
    
    T* allocate(size_t n) {
        void* p = malloc(n * sizeof(T));
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    
    void deallocate(T* p, size_t) noexcept {
        free(p);
    }
    
    template<typename U>
    bool operator==(const Allocator<U>&) const noexcept { return true; }
    
    template<typename U>
    bool operator!=(const Allocator<U>&) const noexcept { return false; }
};

// String in DRAM
class String : public std::basic_string<char, std::char_traits<char>, Allocator<char>> {
    using Base = std::basic_string<char, std::char_traits<char>, Allocator<char>>;
    
public:
    using Base::Base;
    
    String() = default;
    String(const char* s) : Base(s ? s : "") {}
    String(const std::string& s) : Base(s.data(), s.size()) {}
    String(std::string&& s) : Base(s.data(), s.size()) {}
    String(std::string_view sv) : Base(sv.data(), sv.size()) {}
    
    // From Base (result of operator+, substr, etc.)
    String(const Base& b) : Base(b) {}
    String(Base&& b) : Base(std::move(b)) {}
    
    String& operator=(const char* s) {
        if (s) this->assign(s);
        else this->clear();
        return *this;
    }
    
    String& operator=(const std::string& s) {
        this->assign(s.data(), s.size());
        return *this;
    }
    
    String& operator=(const Base& b) {
        Base::operator=(b);
        return *this;
    }
    
    String& operator=(Base&& b) {
        Base::operator=(std::move(b));
        return *this;
    }
    
    std::string str() const { return {this->data(), this->size()}; }
    operator std::string() const { return {this->data(), this->size()}; }
    operator std::string_view() const noexcept { return {this->data(), this->size()}; }
    
    // ArduinoJson / Stream compatibility
    size_t write(uint8_t c) { this->push_back((char)c); return 1; }
    size_t write(const uint8_t* s, size_t n) { this->append((const char*)s, n); return n; }
};

} // namespace M


// ============ P:: PSRAM ============
// For runtime data, after psramInit()

namespace P {

template <typename T>
struct Allocator {
    using value_type = T;
    
    T* allocate(size_t n) {
        size_t bytes = n * sizeof(T);
        void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
        if (!p) {
            p = heap_caps_malloc(bytes, MALLOC_CAP_DEFAULT);
        }
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    
    void deallocate(T* p, size_t) noexcept {
        free(p);
    }
    
    template<typename U>
    bool operator==(const Allocator<U>&) const noexcept { return true; }
    
    template<typename U>
    bool operator!=(const Allocator<U>&) const noexcept { return false; }
};

// String in PSRAM
class String : public std::basic_string<char, std::char_traits<char>, Allocator<char>> {
    using Base = std::basic_string<char, std::char_traits<char>, Allocator<char>>;
    
public:
    using Base::Base;
    
    String() = default;
    String(const char* s) : Base(s ? s : "") {}
    String(const std::string& s) : Base(s.data(), s.size()) {}
    String(std::string&& s) : Base(s.data(), s.size()) {}
    String(std::string_view sv) : Base(sv.data(), sv.size()) {}
    
    // From Base (result of operator+, substr, etc.)
    String(const Base& b) : Base(b) {}
    String(Base&& b) : Base(std::move(b)) {}
    
    // Cross-conversion from M::String
    String(const M::String& s) : Base(s.data(), s.size()) {}
    
    String& operator=(const char* s) {
        if (s) this->assign(s);
        else this->clear();
        return *this;
    }
    
    String& operator=(const std::string& s) {
        this->assign(s.data(), s.size());
        return *this;
    }
    
    String& operator=(const M::String& s) {
        this->assign(s.data(), s.size());
        return *this;
    }
    
    String& operator=(const Base& b) {
        Base::operator=(b);
        return *this;
    }
    
    String& operator=(Base&& b) {
        Base::operator=(std::move(b));
        return *this;
    }
    
    std::string str() const { return {this->data(), this->size()}; }
    operator std::string() const { return {this->data(), this->size()}; }
    operator std::string_view() const noexcept { return {this->data(), this->size()}; }
    
    // ArduinoJson / Stream compatibility
    size_t write(uint8_t c) { this->push_back((char)c); return 1; }
    size_t write(const uint8_t* s, size_t n) { this->append((const char*)s, n); return n; }
};

// Array in PSRAM
template<typename T>
using Array = std::vector<T, Allocator<T>>;

// Map in PSRAM
template<typename K, typename V, typename Compare = std::less<K>>
using Map = std::map<K, V, Compare, Allocator<std::pair<const K, V>>>;

// Deleter for PSRAM objects
template<typename T>
struct Deleter {
    void operator()(T* p) const {
        if (p) {
            p->~T();
            free(p);
        }
    }
};

// Smart pointer to PSRAM object
template<typename T>
using Ptr = std::unique_ptr<T, Deleter<T>>;

// Create object in PSRAM
template<typename T, typename... Args>
Ptr<T> create(Args&&... args) {
    void* p = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM);
    if (!p) {
        p = heap_caps_malloc(sizeof(T), MALLOC_CAP_DEFAULT);
    }
    if (!p) return nullptr;
    return Ptr<T>(new(p) T(std::forward<Args>(args)...));
}

} // namespace P


// ============ MPArray ============
// Vector in DRAM, objects in PSRAM

template<typename T>
using MPArray = std::vector<P::Ptr<T>>;
