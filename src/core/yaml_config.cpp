/**
 * YamlConfig — generic YAML-backed configuration.
 *
 * File format (one level nesting):
 *   display:
 *     brightness: 255
 *   power:
 *     auto_sleep: true
 */

#include "yaml_config.h"
#include "utils/log_config.h"
#include <LittleFS.h>
#include <Arduino.h>

static const char* TAG = "YamlConfig";

YamlConfig::YamlConfig(const char* path, const char* tag)
    : m_store(tag), m_path(path) {}

void YamlConfig::define(const char* key, VarType type, const VarValue& defaultVal) {
    m_store.define(key, type, defaultVal);
}

// ============================================
// Load
// ============================================

bool YamlConfig::load() {
    File f = LittleFS.open(m_path.c_str(), "r");
    if (!f) return false;

    size_t size = f.size();
    if (size == 0 || size > 4096) {
        f.close();
        return false;
    }

    P::String content(size, '\0');
    f.readBytes(&content[0], size);
    f.close();

    return parseYaml(content.c_str());
}

// ============================================
// Save
// ============================================

bool YamlConfig::save() {
    P::String yaml = toYaml();

    // Ensure parent directory exists
    P::String dir = m_path;
    auto slash = dir.rfind('/');
    if (slash != P::String::npos && slash > 0) {
        dir = dir.substr(0, slash);
        if (!LittleFS.exists(dir.c_str())) {
            LittleFS.mkdir(dir.c_str());
        }
    }

    File f = LittleFS.open(m_path.c_str(), "w");
    if (!f) {
        LOG_E(Log::APP, "Cannot write %s", m_path.c_str());
        return false;
    }
    f.print(yaml.c_str());
    f.close();
    LOG_D(Log::APP, "Config saved: %s (%d bytes)", m_path.c_str(), (int)yaml.size());
    return true;
}

// ============================================
// Set + auto-save
// ============================================

void YamlConfig::set(const char* key, const VarValue& value) {
    m_store.set(key, value, false);
    save();
}

void YamlConfig::set(const char* key, const char* value) {
    m_store.setFromString(key, value, false);
    save();
}

// ============================================
// YAML parser
// ============================================

bool YamlConfig::parseYaml(const char* content) {
    P::String section;
    const char* p = content;
    int loaded = 0;

    while (*p) {
        // Skip empty lines
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;

        // Skip comment lines
        if (*p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }

        // Count indent
        int indent = 0;
        while (*p == ' ') { indent++; p++; }
        if (!*p || *p == '\n' || *p == '#') {
            while (*p && *p != '\n') p++;
            continue;
        }

        // Read key (until ':')
        const char* keyStart = p;
        while (*p && *p != ':' && *p != '\n') p++;
        if (*p != ':') continue;

        P::String key(keyStart, p - keyStart);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        p++; // skip ':'

        // Skip spaces after ':'
        while (*p == ' ') p++;

        if (!*p || *p == '\n' || *p == '#') {
            // Section header (no value)
            if (indent == 0) {
                section = key;
            }
            while (*p && *p != '\n') p++;
            continue;
        }

        // Read value
        const char* valStart = p;
        while (*p && *p != '\n' && *p != '#') p++;
        const char* valEnd = p;
        while (valEnd > valStart && (valEnd[-1] == ' ' || valEnd[-1] == '\r')) valEnd--;

        P::String value(valStart, valEnd - valStart);

        // Build full key
        P::String fullKey;
        if (indent >= 2 && !section.empty()) {
            fullKey = section + "." + key;
        } else {
            fullKey = key;
        }

        // Set if defined (respects declared type)
        if (m_store.has(fullKey)) {
            m_store.setFromString(fullKey, value, false);
            loaded++;
        }

        while (*p && *p != '\n') p++;
    }

    LOG_D(Log::APP, "Parsed %d values from %s", loaded, m_path.c_str());
    return loaded > 0;
}

// ============================================
// YAML writer
// ============================================

P::String YamlConfig::toYaml() const {
    P::String yaml;
    yaml.reserve(256);

    P::String lastSection;

    for (size_t i = 0; i < m_store.count(); i++) {
        const P::String& fullKey = m_store.nameAt(i);

        // Split "section.key"
        auto dot = fullKey.find('.');
        P::String section, key;
        if (dot != P::String::npos) {
            section = fullKey.substr(0, dot);
            key = fullKey.substr(dot + 1);
        } else {
            key = fullKey;
        }

        // Section header
        if (!section.empty() && section != lastSection) {
            if (!yaml.empty()) yaml += "\n";
            yaml += section + ":\n";
            lastSection = section;
        }

        // Value
        P::String value = m_store.getAsString(fullKey);
        if (!section.empty()) {
            yaml += "  " + key + ": " + value + "\n";
        } else {
            yaml += key + ": " + value + "\n";
        }
    }

    return yaml;
}

// ============================================
// Debug
// ============================================

void YamlConfig::dump() const {
    Serial.printf("=== %s ===\n", m_path.c_str());
    for (size_t i = 0; i < m_store.count(); i++) {
        const P::String& name = m_store.nameAt(i);
        Serial.printf("  %s = %s\n", name.c_str(), m_store.getAsString(name).c_str());
    }
    Serial.println("==================");
}
