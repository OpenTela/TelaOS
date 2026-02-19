/**
 * file_utils.h — Filesystem utility functions
 *
 * fs_exists() uses stat() instead of LittleFS.exists() to avoid
 * noisy VFS error logs for non-existent files.
 */

#pragma once

#include <sys/stat.h>

/// Silent file existence check (no VFS error spam)
inline bool fs_exists(const char* path) {
    struct stat st;
    // LittleFS paths go through VFS as /littlefs/...
    // stat() returns 0 on success, -1 on error (not found)
    return stat(path, &st) == 0;
}
