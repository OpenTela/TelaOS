#include "utils/string_utils.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace UI {
namespace StringUtils {

void trim(P::String& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    if (start > 0 || end < s.size()) {
        s = P::String(s.data() + start, end - start);
    }
}

P::String trimmed(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
        sv.remove_suffix(1);
    }
    return P::String(sv.data(), sv.size());
}

void toLower(P::String& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
}

P::String toLowerCopy(std::string_view sv) {
    P::String result(sv.data(), sv.size());
    toLower(result);
    return result;
}

bool contains(std::string_view s, char c) {
    return s.find(c) != std::string_view::npos;
}

bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

bool toBool(const char* s) {
    if (!s || !s[0]) return false;
    return (s[0] == 't' || s[0] == 'T' || s[0] == '1');
}

bool toBool(std::string_view sv) {
    if (sv.empty()) return false;
    return (sv[0] == 't' || sv[0] == 'T' || sv[0] == '1');
}

bool equals(const char* a, const char* b) {
    if (!a || !b) return false;
    return std::strcmp(a, b) == 0;
}

bool equals(std::string_view a, const char* b) {
    return a == b;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != 
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

P::String decodeEntities(const P::String& s) {
    if (s.find('&') == P::String::npos) return s; // fast path
    
    P::String out;
    out.reserve(s.size());
    
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            size_t semi = s.find(';', i + 1);
            if (semi != P::String::npos && semi - i < 10) {
                auto entity = s.substr(i + 1, semi - i - 1); // between & and ;
                
                if (entity == "lt")        { out += '<';  i = semi + 1; continue; }
                if (entity == "gt")        { out += '>';  i = semi + 1; continue; }
                if (entity == "amp")       { out += '&';  i = semi + 1; continue; }
                if (entity == "quot")      { out += '"';  i = semi + 1; continue; }
                if (entity == "apos")      { out += '\''; i = semi + 1; continue; }
                if (entity == "nbsp")      { out += ' ';  i = semi + 1; continue; }
                
                // &#NNN; decimal
                if (entity.size() > 1 && entity[0] == '#' && entity[1] != 'x') {
                    int code = std::atoi(entity.c_str() + 1);
                    if (code > 0 && code < 128) { out += (char)code; i = semi + 1; continue; }
                }
                // &#xHH; hex
                if (entity.size() > 2 && entity[0] == '#' && entity[1] == 'x') {
                    int code = (int)std::strtol(entity.c_str() + 2, nullptr, 16);
                    if (code > 0 && code < 128) { out += (char)code; i = semi + 1; continue; }
                }
            }
        }
        out += s[i++];
    }
    return out;
}

} // namespace StringUtils
} // namespace UI
