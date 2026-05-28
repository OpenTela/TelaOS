#pragma once

/**
 * sd_path.h — pure helper to turn a user-supplied path into a safe absolute
 * path under a mount point (e.g. "/sd"). No I/O, no allocation; host-testable.
 *
 * Accepts:  "note.txt", "/note.txt", "a/b.txt", or an already-prefixed
 *           "/sd/note.txt".
 * Rejects:  any ".." path segment (directory traversal) and paths that would
 *           overflow the output buffer.
 */

#include <cstddef>
#include <cstring>
#include <cstdio>

// True if any '/'-delimited segment of `p` is exactly "..".
inline bool sdHasDotDot(const char* p) {
    const char* s = p;
    while (*s) {
        const char* seg = s;
        while (*s && *s != '/') s++;
        size_t len = (size_t)(s - seg);
        if (len == 2 && seg[0] == '.' && seg[1] == '.') return true;
        if (*s == '/') s++;
    }
    return false;
}

// Resolve `in` to `<mount>/<rest>` in `out`. Returns false on reject/overflow.
inline bool sdResolvePath(const char* mount, const char* in,
                          char* out, size_t outsz) {
    if (!mount || !in || !out || outsz == 0) return false;
    if (sdHasDotDot(in)) return false;

    char m[32];
    size_t ml = strlen(mount);
    if (ml == 0 || ml >= sizeof(m)) return false;
    memcpy(m, mount, ml + 1);
    while (ml > 1 && m[ml - 1] == '/') m[--ml] = '\0';  // strip trailing '/'

    // If `in` already begins with the mount point, don't double it.
    const char* rest = in;
    if (strncmp(in, m, ml) == 0 && (in[ml] == '/' || in[ml] == '\0')) {
        rest = in + ml;
    }

    int n;
    if (rest[0] == '\0')        n = snprintf(out, outsz, "%s", m);
    else if (rest[0] == '/')    n = snprintf(out, outsz, "%s%s", m, rest);
    else                        n = snprintf(out, outsz, "%s/%s", m, rest);
    if (n < 0 || (size_t)n >= outsz) return false;
    return true;
}
