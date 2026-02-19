#pragma once
#include <string>
#include <vector>
#include <tuple>
#include <sstream>
#include <type_traits>
#include <LittleFS.h>
#include "csv/mappable.h"
#include "csv/csv_escape.h"
#include "csv/csv_parser.h"

namespace CSV {

// ============================================================================
// Template helpers for type conversion
// ============================================================================

template<typename T>
std::string fieldToString(const T& value) {
    if constexpr (std::is_same_v<T, std::string>) {
        return escape(value);
    } else if constexpr (std::is_same_v<T, int>) {
        return std::to_string(value);
    } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
        return std::to_string(value);
    } else if constexpr (std::is_same_v<T, bool>) {
        return value ? "true" : "false";
    } else {
        static_assert(std::is_same_v<T, std::string>, "Unsupported type");
        return "";
    }
}

template<typename T>
void stringToField(T& field, const std::string& str) {
    if constexpr (std::is_same_v<T, std::string>) {
        field = str;
    } else if constexpr (std::is_same_v<T, int>) {
        field = std::stoi(str);
    } else if constexpr (std::is_same_v<T, float>) {
        field = std::stof(str);
    } else if constexpr (std::is_same_v<T, double>) {
        field = std::stod(str);
    } else if constexpr (std::is_same_v<T, bool>) {
        field = (str == "true" || str == "1");
    } else {
        static_assert(std::is_same_v<T, std::string>, "Unsupported type");
    }
}

// ============================================================================
// Tuple ↔ CSV line
// ============================================================================

template<typename Tuple, size_t... I>
std::string tupleToLineImpl(const Tuple& t, std::index_sequence<I...>) {
    std::vector<std::string> fields = {
        fieldToString(std::get<I>(t))...
    };
    std::string result;
    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) result += DELIMITER;
        result += fields[i];
    }
    return result;
}

template<typename Tuple>
std::string fieldsToLine(const Tuple& fields) {
    return tupleToLineImpl(fields,
        std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

template<typename Tuple, size_t... I>
void assignFieldsImpl(Tuple&& t, const std::vector<std::string>& fields,
                     std::index_sequence<I...>) {
    if (fields.size() != sizeof...(I)) {
        throw std::runtime_error("Field count mismatch");
    }
    ((stringToField(std::get<I>(t), fields[I])), ...);
}

template<typename Tuple>
void assignFields(Tuple&& t, const std::vector<std::string>& fields) {
    assignFieldsImpl(std::forward<Tuple>(t), fields,
        std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<Tuple>>>{});
}

// ============================================================================
// File helpers (LittleFS)
// ============================================================================

inline std::string readFileContents(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    size_t sz = f.size();
    std::string buf(sz, '\0');
    f.readBytes(&buf[0], sz);
    f.close();
    return buf;
}

inline bool writeFileContents(const char* path, const std::string& data) {
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.write((const uint8_t*)data.c_str(), data.size());
    f.close();
    return true;
}

inline bool appendFileContents(const char* path, const std::string& data) {
    File f = LittleFS.open(path, "a");
    if (!f) return false;
    f.write((const uint8_t*)data.c_str(), data.size());
    f.close();
    return true;
}

// ============================================================================
// Main functions: read, write, append
// ============================================================================

template<typename T>
std::vector<T> read(const std::string& path) {
    std::vector<T> result;
    std::string content = readFileContents(path.c_str());
    if (content.empty()) return result;

    std::istringstream stream(content);
    std::string line;
    int lineNum = 0;
    while (std::getline(stream, line)) {
        lineNum++;
        if (line.empty()) continue;

        try {
            auto fields = parseLine(line);
            T obj;
            assignFields(obj.fields(), fields);
            result.push_back(obj);
        } catch (const std::exception& e) {
            fprintf(stderr, "CSV parse error at line %d: %s\n", lineNum, e.what());
        }
    }
    return result;
}

template<typename T>
bool write(const std::string& path, const std::vector<T>& data) {
    std::string content;
    for (const auto& obj : data) {
        content += fieldsToLine(const_fields_of(obj));
        content += "\n";
    }
    return writeFileContents(path.c_str(), content);
}

template<typename T>
bool append(const std::string& path, const T& obj) {
    std::string line = fieldsToLine(const_fields_of(obj));
    line += "\n";
    return appendFileContents(path.c_str(), line);
}

} // namespace CSV
