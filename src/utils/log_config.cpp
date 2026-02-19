#include "utils/log_config.h"
#include <cstring>

namespace Log {

// Defaults: Info для основных, Warn для шумных
static Level s_levels[COUNT] = { Info, Info, Info, Info, Info };
// All categories default to Info

void set(Cat cat, Level lvl) {
    if (cat < COUNT) s_levels[cat] = lvl;
}

void setAll(Level lvl) {
    for (int i = 0; i < COUNT; i++) s_levels[i] = lvl;
}

Level get(Cat cat) {
    return (cat < COUNT) ? s_levels[cat] : Disabled;
}

const char* catName(Cat cat) {
    static const char* names[] = { "UI", "LUA", "STATE", "APP", "BLE" };
    return (cat < COUNT) ? names[cat] : "?";
}

const char* levelName(Level lvl) {
    static const char* names[] = { "Disabled", "Error", "Warn", "Info", "Debug", "Verbose" };
    return (lvl <= Verbose) ? names[lvl] : "?";
}

static Level parseLevel(const char* s) {
    if (!s || !s[0]) return Info;
    
    char c = s[0] | 0x20; // lowercase first char
    if (c == 'd' && (s[1] | 0x20) == 'i') return Disabled; // "disabled" vs "debug"
    if (c == 'o') return Disabled;  // off
    if (c == 'n') return Disabled;  // none
    if (c == 'e') return Error;
    if (c == 'w') return Warn;
    if (c == 'i') return Info;
    if (c == 'd') return Debug;
    if (c == 'v') return Verbose;
    if (c == 'a') return Verbose;   // all
    
    return Info;
}

static int parseCat(const char* s) {
    if (!s || !s[0]) return -1;
    
    char c = s[0] | 0x20;
    if (c == 'u') return UI;
    if (c == 'l') return LUA;
    if (c == 's') return STATE;
    if (c == 'a') return APP;
    if (c == 'b') return BLE;
    
    return -1;
}

static bool isLevel(const char* s) {
    if (!s || !s[0]) return false;
    char c = s[0] | 0x20;
    return c == 'd' || c == 'e' || c == 'w' || c == 'i' || c == 'v' || 
           c == 'o' || c == 'n' || c == 'a';
}

static void print() {
    log_printf("[Log]\r\n");
    for (int i = 0; i < COUNT; i++) {
        log_printf("  %s = %s\r\n", catName((Cat)i), levelName(s_levels[i]));
    }
}

void command(const char* args) {
    // Skip spaces
    while (args && *args == ' ') args++;
    
    if (!args || !args[0]) {
        print();
        return;
    }
    
    // Копируем для токенизации
    char buf[32];
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    char* tok1 = strtok(buf, " ");
    char* tok2 = strtok(nullptr, " ");
    
    if (!tok2) {
        // Один аргумент — уровень для всех
        if (isLevel(tok1)) {
            Level lvl = parseLevel(tok1);
            setAll(lvl);
            log_printf("[Log] ALL -> %s\r\n", levelName(lvl));
        } else {
            log_printf("[Log] Unknown: %s\r\n", tok1);
        }
        return;
    }
    
    // Два аргумента — категория + уровень
    int cat = parseCat(tok1);
    if (cat < 0) {
        log_printf("[Log] Unknown category: %s\r\n", tok1);
        return;
    }
    
    Level lvl = parseLevel(tok2);
    set((Cat)cat, lvl);
    log_printf("[Log] %s -> %s\r\n", catName((Cat)cat), levelName(lvl));
}

} // namespace Log
