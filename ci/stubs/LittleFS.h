#pragma once
/**
 * LittleFS mock — In-memory filesystem for testing.
 *
 * Drop-in replacement for ESP32 LittleFS.
 * Storage: files = map<path, bytes>, dirs = set<path>
 *
 * Test helpers (not on real ESP32):
 *   LittleFS.reset();                         // clear all
 *   LittleFS.writeFile("/path", "content");   // quick write
 *   std::string s = LittleFS.readFile("/p");  // quick read
 */
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>

// ============ Path normalization ============

inline std::string fs_normalize(const char* path) {
    std::string p(path);
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

// Forward declaration
class LittleFSClass;
extern LittleFSClass LittleFS;

// ============ File Class ============

class File {
    friend class LittleFSClass;

    std::string m_path;
    std::vector<uint8_t>* m_data = nullptr;
    std::vector<uint8_t> m_writeData;
    size_t m_pos = 0;
    bool m_valid = false;
    bool m_isDir = false;
    bool m_writing = false;

    std::vector<std::string> m_entries;
    size_t m_entryIdx = 0;

public:
    File() = default;

    operator bool() const { return m_valid; }

    size_t size() const {
        if (m_data) return m_data->size();
        return 0;
    }

    void close();  // defined after LittleFSClass

    size_t read(uint8_t* buf, size_t len) {
        if (!m_data) return 0;
        size_t avail = m_data->size() - m_pos;
        size_t n = (len < avail) ? len : avail;
        if (n > 0) {
            memcpy(buf, m_data->data() + m_pos, n);
            m_pos += n;
        }
        return n;
    }

    size_t readBytes(char* buf, size_t len) {
        return read((uint8_t*)buf, len);
    }

    size_t write(const uint8_t* buf, size_t len) {
        if (!m_writing) return 0;
        m_writeData.insert(m_writeData.end(), buf, buf + len);
        return len;
    }

    size_t print(const char* s) {
        if (!s) return 0;
        return write((const uint8_t*)s, strlen(s));
    }

    size_t println(const char* s = "") {
        size_t n = print(s);
        n += write((const uint8_t*)"\n", 1);
        return n;
    }

    bool available() const {
        if (!m_data) return false;
        return m_pos < m_data->size();
    }

    bool isDirectory() const { return m_isDir; }

    File openNextFile();  // defined after LittleFSClass

    const char* name() const {
        size_t slash = m_path.rfind('/');
        if (slash != std::string::npos) return m_path.c_str() + slash + 1;
        return m_path.c_str();
    }

    const char* path() const { return m_path.c_str(); }
};

// ============ LittleFSClass ============

class LittleFSClass {
public:
    // --- Internal storage ---
    std::map<std::string, std::vector<uint8_t>> files;
    std::set<std::string> dirs;
    bool mounted = false;

    // --- Standard ESP32 LittleFS API ---

    bool begin(bool formatOnFail = false, const char* basePath = "/littlefs", uint8_t maxOpenFiles = 10, const char* partitionLabel = nullptr) {
        mounted = true;
        return true;
    }

    bool exists(const char* path) {
        std::string p = fs_normalize(path);
        return files.count(p) > 0 || dirs.count(p) > 0;
    }

    File open(const char* path, const char* mode = "r") {
        std::string p = fs_normalize(path);
        File f;
        f.m_path = p;

        if (mode && mode[0] == 'w') {
            f.m_valid = true;
            f.m_writing = true;
            f.m_writeData.clear();
            return f;
        }
        if (mode && mode[0] == 'a') {
            f.m_valid = true;
            f.m_writing = true;
            auto it = files.find(p);
            if (it != files.end()) {
                f.m_writeData = it->second;
            }
            return f;
        }
        // Read mode
        if (dirs.count(p) > 0) {
            f.m_isDir = true;
            f.m_valid = true;
            f.m_entries = listDir(p.c_str());
            f.m_entryIdx = 0;
            return f;
        }
        auto it = files.find(p);
        if (it != files.end()) {
            f.m_data = &it->second;
            f.m_valid = true;
        }
        return f;
    }

    bool mkdir(const char* path) {
        dirs.insert(fs_normalize(path));
        return true;
    }

    bool remove(const char* path) {
        files.erase(fs_normalize(path));
        return true;
    }

    bool rmdir(const char* path) {
        dirs.erase(fs_normalize(path));
        return true;
    }

    size_t usedBytes() {
        size_t total = 0;
        for (auto& [k, data] : files) {
            total += data.size();
        }
        return total;
    }

    size_t totalBytes() { return 1000000; }

    // --- Test helpers (not on real ESP32) ---

    void reset() {
        files.clear();
        dirs.clear();
        mounted = false;
    }

    void writeFile(const char* path, const char* content) {
        std::string p = fs_normalize(path);
        size_t len = strlen(content);
        files[p] = std::vector<uint8_t>(content, content + len);
        autoCreateParentDirs(p);
    }

    void writeFile(const char* path, const uint8_t* data, size_t len) {
        std::string p = fs_normalize(path);
        files[p] = std::vector<uint8_t>(data, data + len);
        autoCreateParentDirs(p);
    }

    // Read entire file as string (returns "" if missing)
    std::string readFile(const char* path) {
        std::string p = fs_normalize(path);
        auto it = files.find(p);
        if (it == files.end()) return "";
        return std::string(it->second.begin(), it->second.end());
    }

    bool fileExists(const char* path) {
        return files.count(fs_normalize(path)) > 0;
    }

    // List immediate children of a directory
    std::vector<std::string> listDir(const char* dirPath) {
        std::vector<std::string> result;
        std::string prefix(dirPath);
        if (!prefix.empty() && prefix.back() != '/') prefix += '/';

        std::set<std::string> seen;

        for (auto& [path, data] : files) {
            if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix) {
                size_t slash = path.find('/', prefix.size());
                std::string child = (slash == std::string::npos) ? path : path.substr(0, slash);
                if (seen.insert(child).second) result.push_back(child);
            }
        }

        for (auto& d : dirs) {
            if (d.size() > prefix.size() && d.substr(0, prefix.size()) == prefix) {
                size_t slash = d.find('/', prefix.size());
                std::string child = (slash == std::string::npos) ? d : d.substr(0, slash);
                if (seen.insert(child).second) result.push_back(child);
            }
        }

        return result;
    }

private:
    void autoCreateParentDirs(const std::string& p) {
        for (size_t i = 1; i < p.size(); i++) {
            if (p[i] == '/') {
                dirs.insert(p.substr(0, i));
            }
        }
    }
};

// ============ Deferred File methods ============

inline void File::close() {
    if (m_writing && m_valid) {
        LittleFS.files[m_path] = std::move(m_writeData);
    }
    m_valid = false;
    m_data = nullptr;
}

inline File File::openNextFile() {
    if (!m_isDir || m_entryIdx >= m_entries.size()) return File();
    const std::string& entry = m_entries[m_entryIdx++];
    if (LittleFS.dirs.count(entry) > 0) {
        File f;
        f.m_path = entry;
        f.m_isDir = true;
        f.m_valid = true;
        f.m_entries = LittleFS.listDir(entry.c_str());
        f.m_entryIdx = 0;
        return f;
    }
    File f;
    f.m_path = entry;
    auto it = LittleFS.files.find(entry);
    if (it != LittleFS.files.end()) {
        f.m_data = &it->second;
        f.m_valid = true;
    }
    return f;
}
