#pragma once
#include <string>

namespace CSV {

// Экранирование строки для CSV с escape sequences
inline std::string escape(const std::string& str) {
    std::string result;
    bool needsQuotes = false;
    
    // Заменяем спецсимволы на escape sequences
    for (char c : str) {
        if (c == '\\') {
            result += "\\\\";
            needsQuotes = true;
        } else if (c == '\n') {
            result += "\\n";
            needsQuotes = true;
        } else if (c == '\r') {
            result += "\\r";
            needsQuotes = true;
        } else if (c == '\t') {
            result += "\\t";
            needsQuotes = true;
        } else if (c == '"') {
            result += "\"\"";
            needsQuotes = true;
        } else if (c == ';') {
            result += c;
            needsQuotes = true;
        } else {
            result += c;
        }
    }
    
    // Оборачиваем в кавычки если есть спецсимволы
    if (needsQuotes) {
        return "\"" + result + "\"";
    }
    
    return result;
}

// Снятие экранирования
inline std::string unescape(const std::string& str) {
    if (str.empty()) return str;
    
    // Убираем пробелы по краям
    size_t start = 0;
    size_t end = str.length();
    while (start < end && (str[start] == ' ' || str[start] == '\t')) start++;
    while (end > start && (str[end-1] == ' ' || str[end-1] == '\t')) end--;
    
    std::string trimmed = str.substr(start, end - start);
    
    // Если не в кавычках - возвращаем как есть
    if (trimmed.length() < 2 || trimmed[0] != '"' || trimmed[trimmed.length()-1] != '"') {
        return trimmed;
    }
    
    // Убираем кавычки и декодируем
    std::string result;
    for (size_t i = 1; i < trimmed.length() - 1; i++) {
        if (trimmed[i] == '\\' && i + 1 < trimmed.length() - 1) {
            // Escape sequence - проверяем следующий символ
            char next = trimmed[i + 1];
            
            // ВАЖНО: проверять \\ ПЕРВЫМ!
            if (next == '\\') {
                result += '\\';
                i++;
            } else if (next == 'n') {
                result += '\n';
                i++;
            } else if (next == 'r') {
                result += '\r';
                i++;
            } else if (next == 't') {
                result += '\t';
                i++;
            } else {
                // Неизвестный escape - оставляем бэкслеш
                result += '\\';
            }
        } else if (trimmed[i] == '"' && i + 1 < trimmed.length() - 1 && trimmed[i+1] == '"') {
            // Двойная кавычка
            result += '"';
            i++;
        } else {
            result += trimmed[i];
        }
    }
    
    return result;
}

} // namespace CSV
