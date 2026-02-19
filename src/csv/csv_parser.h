#pragma once
#include <string>
#include <vector>
#include "csv_escape.h"

namespace CSV {

// Разделитель полей (точка с запятой для европейской локали)
constexpr char DELIMITER = ';';

// Парсинг одной строки CSV
inline std::vector<std::string> parseLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;
    
    for (size_t i = 0; i < line.length(); i++) {
        char c = line[i];
        
        if (c == '"') {
            if (inQuotes && i + 1 < line.length() && line[i+1] == '"') {
                // Двойная кавычка внутри поля
                current += '"';
                i++;  // Пропускаем следующую кавычку
            } else {
                // Начало или конец экранированного поля
                inQuotes = !inQuotes;
                current += c;
            }
        } else if (c == DELIMITER && !inQuotes) {
            // Разделитель полей
            fields.push_back(unescape(current));
            current.clear();
        } else if ((c == '\n' || c == '\r') && !inQuotes) {
            // Конец строки
            break;
        } else {
            current += c;
        }
    }
    
    // Добавляем последнее поле
    if (!current.empty() || !fields.empty()) {
        fields.push_back(unescape(current));
    }
    
    return fields;
}

} // namespace CSV
