#include "ui/xml_utils.h"
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace UI {
namespace XmlUtils {

// Tags are compared in place, not against "<tag" and "</tag>" printed with snprintf: its
// formatter takes a kilobyte of stack and more, and pages are read deep in it (sim/sim_stack.h)
namespace {

// "<tag" at p
bool opens(const char* p, const char* tag, size_t tlen) {
    return p[0] == '<' && std::strncmp(p + 1, tag, tlen) == 0;
}

// "</tag>" at p
bool closes(const char* p, const char* tag, size_t tlen) {
    return p[0] == '<' && p[1] == '/' && std::strncmp(p + 2, tag, tlen) == 0 && p[tlen + 2] == '>';
}

}  // namespace

const char* findTagOpen(const char* p, const char* tag) {
    if (!p || !tag) return nullptr;

    const size_t tlen = std::strlen(tag);
    for (; (p = std::strchr(p, '<')) != nullptr; ++p) {
        if (!opens(p, tag, tlen))
            continue;
        const char c = p[tlen + 1];
        if (c == '>' || c == ' ' || c == '/' || c == '\0') {
            return p;
        }
    }
    return nullptr;
}

const char* findTagClose(const char* p, const char* tag) {
    if (!p || !tag) return nullptr;

    int depth = 1;
    size_t tlen = std::strlen(tag);

    while (*p && depth > 0) {

        if (closes(p, tag, tlen)) {
            depth--;
            if (depth == 0) return p;
            p += tlen + 3;
        } else if (opens(p, tag, tlen)) {
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
        if (p >= end || *p != '=') {
            // An attribute without a value (`checked`, `open`): the next one starts right here.
            // Step over a character only if no name was read (a stray '>'), else this would loop.
            if (alen == 0) p++;
            continue;
        }
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
