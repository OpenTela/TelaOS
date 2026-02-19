#include "ui/xml_utils.h"
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace UI {
namespace XmlUtils {

const char* findTagOpen(const char* p, const char* tag) {
    if (!p || !tag) return nullptr;
    
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "<%s", tag);
    size_t tlen = std::strlen(tag);
    
    while ((p = std::strstr(p, pattern)) != nullptr) {
        char c = p[tlen + 1];
        if (c == '>' || c == ' ' || c == '/' || c == '\0') {
            return p;
        }
        p++;
    }
    return nullptr;
}

const char* findTagClose(const char* p, const char* tag) {
    if (!p || !tag) return nullptr;
    
    int depth = 1;
    size_t tlen = std::strlen(tag);
    
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    
    int iterations = 0;
    const int MAX_ITERATIONS = 10000;
    
    while (*p && depth > 0) {
        iterations++;
        if (iterations > MAX_ITERATIONS) {
            return nullptr;
        }
        
        if (std::strncmp(p, close, tlen + 3) == 0) {
            depth--;
            if (depth == 0) return p;
            p += tlen + 3;
        } else if (std::strncmp(p, open, tlen + 1) == 0) {
            char c = p[tlen + 1];
            if (c == '>' || c == ' ' || c == '/') {
                depth++;
            }
            p++;
        } else {
            p++;
        }
    }
    return nullptr;
}

size_t tagOpenLen(const char* tag) {
    return std::strlen(tag) + 1;
}

size_t tagCloseLen(const char* tag) {
    return std::strlen(tag) + 3;
}

P::String getAttr(const char* start, const char* end, const char* name) {
    if (!start || !end || !name || start >= end) return "";
    
    size_t nlen = std::strlen(name);
    const char* p = start;
    
    while (p < end) {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) p++;
        if (p >= end) break;
        
        const char* astart = p;
        while (p < end && *p != '=' && !std::isspace(static_cast<unsigned char>(*p)) && *p != '>') p++;
        size_t alen = p - astart;
        
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) p++;
        if (p >= end || *p != '=') { p++; continue; }
        p++;
        
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) p++;
        
        char q = 0;
        if (*p == '"' || *p == '\'') q = *p++;
        
        const char* vstart = p;
        if (q) {
            while (p < end && *p != q) p++;
        } else {
            while (p < end && !std::isspace(static_cast<unsigned char>(*p)) && *p != '>') p++;
        }
        size_t vlen = p - vstart;
        if (q && p < end) p++;
        
        if (alen == nlen && std::strncmp(astart, name, nlen) == 0) {
            return P::String(vstart, vlen);
        }
    }
    return "";
}

const char* getAttr(const char* start, const char* end, const char* name, char* out, size_t maxLen) {
    out[0] = '\0';
    P::String value = getAttr(start, end, name);
    if (value.empty()) return nullptr;
    
    size_t copyLen = std::min(value.size(), maxLen - 1);
    std::strncpy(out, value.c_str(), copyLen);
    out[copyLen] = '\0';
    return out;
}

bool hasAttr(const char* start, const char* end, const char* name) {
    return !getAttr(start, end, name).empty();
}

int getAttrInt(const char* start, const char* end, const char* name, int defaultVal) {
    P::String val = getAttr(start, end, name);
    if (val.empty()) return defaultVal;
    return std::atoi(val.c_str());
}

bool getAttrBool(const char* start, const char* end, const char* name, bool defaultVal) {
    P::String val = getAttr(start, end, name);
    if (val.empty()) return defaultVal;
    return (val[0] == 't' || val[0] == 'T' || val[0] == '1');
}

} // namespace XmlUtils
} // namespace UI
