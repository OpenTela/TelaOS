/**
 * ui_launcher.cpp - Launcher UI class implementation
 */

#include "ui/ui_launcher.h"
#include "ui/ui_engine.h"
#include "ui/ui_shade.h"
#include "core/sys_paths.h"
#include "widgets/widget_common.h"
#include "hal/display_hal.h"
#include "core/app_manager.h"
#include "utils/font.h"
#include "utils/log_config.h"

#include <lvgl.h>
#include <LittleFS.h>
#include <Arduino.h>
#include <cstring>
#include <cctype>
#include <ctime>

#include "ui_layout.h"
#include "_generated_icons.h"

static const char* TAG = "Launcher";

namespace UI {

// ============================================
// Time helpers
// ============================================

// Time is "synced" if system clock is past 2024-01-01
static bool isTimeSynced() {
    return time(nullptr) > 1704067200;  // 2024-01-01 00:00:00 UTC
}

static struct tm* getLocalNow() {
    time_t now = time(nullptr);
    return localtime(&now);
}

static const char* s_dayNames[]      = { "Воскресенье", "Понедельник", "Вторник", "Среда", "Четверг", "Пятница", "Суббота" };
static const char* s_dayNamesLower[] = { "воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота" };
static const char* s_monthShort[]    = { "Янв", "Фев", "Мар", "Апр", "Май", "Июн", "Июл", "Авг", "Сен", "Окт", "Ноя", "Дек" };
static const char* s_monthGenitive[] = { "января", "февраля", "марта", "апреля", "мая", "июня",
                                         "июля", "августа", "сентября", "октября", "ноября", "декабря" };

const char* Launcher::getTimeStr() {
    static char buf[16];
    if (isTimeSynced()) {
        struct tm* t = getLocalNow();
        snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
    } else {
        // Fallback: uptime counter
        if (!m_timeInitialized) {
            m_startMillis = millis();
            m_timeInitialized = true;
        }
        uint32_t elapsed = (millis() - m_startMillis) / 1000;
        snprintf(buf, sizeof(buf), "%02d:%02d", (int)((elapsed / 3600) % 24), (int)((elapsed / 60) % 60));
    }
    return buf;
}

const char* Launcher::getDayName() {
    if (!isTimeSynced()) return "Понедельник";
    return s_dayNames[getLocalNow()->tm_wday];
}

const char* Launcher::getDayNameLower() {
    if (!isTimeSynced()) return "понедельник";
    return s_dayNamesLower[getLocalNow()->tm_wday];
}

int Launcher::getDay() {
    if (!isTimeSynced()) return 1;
    return getLocalNow()->tm_mday;
}

const char* Launcher::getMonthShort() {
    if (!isTimeSynced()) return "Янв";
    return s_monthShort[getLocalNow()->tm_mon];
}

const char* Launcher::getDateLower() {
    static char buf[32];
    if (!isTimeSynced()) return "1 января";
    struct tm* t = getLocalNow();
    snprintf(buf, sizeof(buf), "%d %s", t->tm_mday, s_monthGenitive[t->tm_mon]);
    return buf;
}

// ============================================
// Title truncation (UTF-8 aware)
// ============================================

P::String Launcher::truncateTitle(const P::String& title, size_t maxChars) {
    if (title.empty()) return title;
    
    size_t charCount = 0;
    size_t bytePos = 0;
    
    while (bytePos < title.size() && charCount < maxChars) {
        uint8_t c = (uint8_t)title[bytePos];
        if ((c & 0x80) == 0) bytePos += 1;
        else if ((c & 0xE0) == 0xC0) bytePos += 2;
        else if ((c & 0xF0) == 0xE0) bytePos += 3;
        else if ((c & 0xF8) == 0xF0) bytePos += 4;
        else bytePos += 1;
        charCount++;
    }
    
    if (bytePos >= title.size()) return title;
    
    charCount = 0;
    bytePos = 0;
    size_t targetChars = (maxChars > 2) ? (maxChars - 2) : 1;
    
    while (bytePos < title.size() && charCount < targetChars) {
        uint8_t c = (uint8_t)title[bytePos];
        if ((c & 0x80) == 0) bytePos += 1;
        else if ((c & 0xE0) == 0xC0) bytePos += 2;
        else if ((c & 0xF0) == 0xE0) bytePos += 3;
        else if ((c & 0xF8) == 0xF0) bytePos += 4;
        else bytePos += 1;
        charCount++;
    }
    
    return P::String(title.c_str(), bytePos) + "..";
}

// ============================================
// Icon loading
// ============================================

lv_obj_t* Launcher::loadIcon(lv_obj_t* cell, const LauncherAppInfo& app, size_t appIdx) {
    const lv_image_dsc_t* iconSrc = nullptr;
    
#ifdef USE_BUILTIN_ICONS
    // 1. Builtin app icon (with staleness check)
    {
        const BuiltinIcon* entry = findBuiltinEntry(app.name.c_str());
        if (entry) {
            // Verify icon hasn't changed on FS since build
            bool stale = false;
            if (!app.iconPath.empty()) {
                P::String fsPath = app.iconPath;
                if (fsPath.size() > 2 && fsPath[0] == 'C' && fsPath[1] == ':')
                    fsPath = fsPath.substr(2);
                File f = LittleFS.open(fsPath.c_str(), "r");
                if (f) { stale = ((uint32_t)f.size() != entry->png_size); f.close(); }
            }
            if (!stale) iconSrc = entry->icon;
        }
    }
    
    // 2. Builtin system icon (with staleness check)
    if (!iconSrc) {
        const char* sysPrefix = SYS_LVGL_PREFIX SYS_ICONS;
        if (strncmp(app.iconPath.c_str(), sysPrefix, strlen(sysPrefix)) == 0) {
            const char* nameStart = app.iconPath.c_str() + strlen(sysPrefix);
            P::String iconName = nameStart;
            size_t dotPos = iconName.rfind('.');
            if (dotPos != P::String::npos) iconName = iconName.substr(0, dotPos);
            
            const BuiltinIcon* entry = findSystemEntry(iconName.c_str());
            if (entry) {
                bool stale = false;
                P::String fsPath = app.iconPath;
                if (fsPath.size() > 2 && fsPath[0] == 'C' && fsPath[1] == ':')
                    fsPath = fsPath.substr(2);
                File f = LittleFS.open(fsPath.c_str(), "r");
                if (f) { stale = ((uint32_t)f.size() != entry->png_size); f.close(); }
                if (!stale) iconSrc = entry->icon;
            }
        }
    }
#endif

#ifndef NO_PNG_ICONS
    // 3. PSRAM preloaded
    if (!iconSrc) {
  #ifdef PRELOAD_ICONS_TO_PSRAM
        if (app.hasIcon && app.iconDsc) {
            iconSrc = app.iconDsc;
        }
  #endif
    }
    
    // 4. Filesystem icon
    if (!iconSrc && !app.iconPath.empty()) {
        P::String fsPath = app.iconPath;
        if (fsPath.size() > 2 && fsPath[0] == 'C' && fsPath[1] == ':') {
            fsPath = fsPath.substr(2);
        }
        if (LittleFS.exists(fsPath.c_str())) {
            lv_obj_t* icon = lv_image_create(cell);
            lv_obj_remove_style_all(icon);
            lv_image_set_src(icon, app.iconPath.c_str());
            lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
            return icon;
        }
    }
#endif

#ifdef USE_BUILTIN_ICONS
    // 5. Builtin category icon
    if (!iconSrc && !app.category.empty()) {
        iconSrc = findCategoryIcon(app.category.c_str());
    }
#endif
    
    // Create from descriptor
    if (iconSrc) {
        lv_obj_t* icon = lv_image_create(cell);
        lv_obj_remove_style_all(icon);
        lv_image_set_src(icon, iconSrc);
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
        return icon;
    }
    
#if !defined(NO_PNG_ICONS) && !defined(PRELOAD_ICONS_TO_PSRAM)
    // 6. Filesystem category icon
    if (!app.category.empty()) {
        char buf[64];
        snprintf(buf, sizeof(buf), SYS_ICONS "%s.png", app.category.c_str());
        if (LittleFS.exists(buf)) {
            lv_obj_t* icon = lv_image_create(cell);
            lv_obj_remove_style_all(icon);
            char lvglPath[72];
            snprintf(lvglPath, sizeof(lvglPath), SYS_LVGL_PREFIX "%s", buf);
            lv_image_set_src(icon, lvglPath);
            lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 0);
            return icon;
        }
    }
#endif
    
    // 7. Letter fallback
    lv_obj_t* letter = lv_label_create(cell);
    char txt[2] = {(char)toupper(app.title[0]), 0};
    lv_label_set_text(letter, txt);
    lv_obj_set_style_text_color(letter, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(letter, &Ubuntu_48px, 0);
    lv_obj_align(letter, LV_ALIGN_TOP_MID, 0, LAUNCHER_LETTER_TOP_OFFSET);
    return nullptr;  // Letter is not an icon widget
}

// ============================================
// Static callbacks -> instance methods
// ============================================

void Launcher::onGestureStatic(lv_event_t* e) {
    auto* self = (Launcher*)lv_event_get_user_data(e);
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    self->onGesture(dir);
}

void Launcher::onTouchStartStatic(lv_event_t* e) {
    auto* self = (Launcher*)lv_event_get_user_data(e);
    lv_indev_t* indev = lv_indev_active();
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        self->onTouchStart(p.x, p.y);
    }
}

void Launcher::onCellClickStatic(lv_event_t* e) {
    auto* data = (Launcher::CellData*)lv_event_get_user_data(e);
    auto* self = data->self;
    size_t appIdx = data->appIdx;
    
    lv_indev_t* indev = lv_indev_active();
    if (!indev || self->m_touchStartX < 0) {
        self->m_touchStartX = -1;
        return;
    }
    
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    uint32_t duration = lv_tick_get() - self->m_touchStartTime;
    
    // Filter too short clicks
    if (duration < 60) {
        self->m_touchStartX = -1;
        return;
    }
    
    // Filter swipes
    int dx = abs(p.x - self->m_touchStartX);
    int dy = abs(p.y - self->m_touchStartY);
    const int SWIPE_THRESHOLD = SCREEN_WIDTH * 8 / 100;
    
    if (dx > SWIPE_THRESHOLD || dy > SWIPE_THRESHOLD) {
        self->m_touchStartX = -1;
        return;
    }
    
    self->m_touchStartX = -1;
    self->onCellClick(appIdx, duration);
}

void Launcher::updateClocksStatic(lv_timer_t* t) {
    auto* self = (Launcher*)lv_timer_get_user_data(t);
    self->updateClocks();
}

// ============================================
// Instance callback methods
// ============================================

void Launcher::onTouchStart(int16_t x, int16_t y) {
    m_touchStartX = x;
    m_touchStartY = y;
    m_touchStartTime = lv_tick_get();
}

void Launcher::onGesture(lv_dir_t dir) {
    m_touchStartX = -1;
    
    // Swipe down → open shade
    if (dir == LV_DIR_BOTTOM) {
        Shade::open();
        return;
    }
    
    if (m_pages.empty()) return;
    
    size_t newPage = m_currentPage;
    
    if (dir == LV_DIR_LEFT && m_currentPage < m_numPages - 1) {
        newPage = m_currentPage + 1;
    } else if (dir == LV_DIR_RIGHT && m_currentPage > 0) {
        newPage = m_currentPage - 1;
    }
    
    if (newPage != m_currentPage) {
        lv_obj_add_flag(m_pages[m_currentPage], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_pages[newPage], LV_OBJ_FLAG_HIDDEN);
        
        if (m_dotsContainer && !LAUNCHER_SHOW_DOTS_PAGE1) {
            if (newPage == 0) {
                lv_obj_add_flag(m_dotsContainer, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(m_dotsContainer, LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        for (size_t i = 0; i < m_dots.size(); i++) {
            if (i == newPage) {
                lv_obj_set_style_bg_color(m_dots[i], lv_color_hex(0xffffff), 0);
                lv_obj_set_style_shadow_color(m_dots[i], lv_color_hex(0xffffff), 0);
                lv_obj_set_style_shadow_width(m_dots[i], DOT_SIZE, 0);
                lv_obj_set_style_shadow_opa(m_dots[i], LV_OPA_50, 0);
            } else {
                lv_obj_set_style_bg_color(m_dots[i], lv_color_hex(0x444455), 0);
                lv_obj_set_style_shadow_width(m_dots[i], 0, 0);
            }
        }
        m_currentPage = newPage;
    }
}

void Launcher::onCellClick(size_t appIdx, uint32_t duration) {
    if (appIdx < m_apps.size()) {
        LOG_I(Log::APP, "Launch: %s (dur=%dms)", m_apps[appIdx].name.c_str(), (int)duration);
        App::Manager::instance().queueLaunch(m_apps[appIdx].name);
    }
}

void Launcher::updateClocks() {
    const char* timeStr = getTimeStr();
    for (auto* lbl : m_clockLabels) {
        if (lbl) lv_label_set_text(lbl, timeStr);
    }
    
    // Update date labels once per minute (or when time just synced)
    static int lastMin = -1;
    struct tm* t = isTimeSynced() ? getLocalNow() : nullptr;
    int curMin = t ? t->tm_min : -1;
    
    if (curMin != lastMin) {
        lastMin = curMin;
        
        // Big date: "Понедельник, 24 Фев"
        char bigDateBuf[64];
        snprintf(bigDateBuf, sizeof(bigDateBuf), "%s, %d %s", getDayName(), getDay(), getMonthShort());
        for (auto* lbl : m_bigDateLabels) {
            if (lbl) lv_label_set_text(lbl, bigDateBuf);
        }
        
        // Compact day: "понедельник"
        const char* dayLower = getDayNameLower();
        for (auto* lbl : m_compactDayLabels) {
            if (lbl) lv_label_set_text(lbl, dayLower);
        }
        
        // Compact date: "24 февраля"
        const char* dateLower = getDateLower();
        for (auto* lbl : m_compactDateLabels) {
            if (lbl) lv_label_set_text(lbl, dateLower);
        }
    }
}

// ============================================
// Clock creation
// ============================================

void Launcher::createBigClock(lv_obj_t* page) {
    lv_obj_t* bigTime = lv_label_create(page);
    lv_label_set_text(bigTime, getTimeStr());
    lv_obj_set_style_text_color(bigTime, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(bigTime, &Ubuntu_72px, 0);
    lv_obj_align(bigTime, LV_ALIGN_TOP_MID, 0, CLOCK_TOP_OFFSET);
    lv_obj_add_flag(bigTime, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(bigTime, LV_OBJ_FLAG_EVENT_BUBBLE);
    m_clockLabels.push_back(bigTime);
    
    char dateBuf[64];
    snprintf(dateBuf, sizeof(dateBuf), "%s, %d %s", getDayName(), getDay(), getMonthShort());
    
    lv_obj_t* bigDate = lv_label_create(page);
    lv_label_set_text(bigDate, dateBuf);
    lv_obj_set_style_text_color(bigDate, lv_color_hex(0x888899), 0);
    lv_obj_set_style_text_font(bigDate, &Ubuntu_16px, 0);
    lv_obj_align(bigDate, LV_ALIGN_TOP_MID, 0, DATE_TOP_OFFSET);
    lv_obj_add_flag(bigDate, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(bigDate, LV_OBJ_FLAG_EVENT_BUBBLE);
    m_bigDateLabels.push_back(bigDate);
}

void Launcher::createCompactClock(lv_obj_t* page) {
    if (!LAUNCHER_SHOW_COMPACT_CLOCK) return;
    
    lv_obj_t* compactTime = lv_label_create(page);
    lv_label_set_text(compactTime, getTimeStr());
    lv_obj_set_style_text_color(compactTime, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(compactTime, &Ubuntu_32px, 0);
    lv_obj_set_pos(compactTime, SCALED(130), SCALED_H(22));
    lv_obj_add_flag(compactTime, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(compactTime, LV_OBJ_FLAG_EVENT_BUBBLE);
    m_clockLabels.push_back(compactTime);
    
    lv_obj_t* compactDay = lv_label_create(page);
    lv_label_set_text(compactDay, getDayNameLower());
    lv_obj_set_style_text_color(compactDay, lv_color_hex(0x888899), 0);
    lv_obj_set_style_text_font(compactDay, &Ubuntu_16px, 0);
    lv_obj_set_pos(compactDay, SCALED(250), SCALED_H(19));
    lv_obj_add_flag(compactDay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(compactDay, LV_OBJ_FLAG_EVENT_BUBBLE);
    m_compactDayLabels.push_back(compactDay);
    
    lv_obj_t* compactDate = lv_label_create(page);
    lv_label_set_text(compactDate, getDateLower());
    lv_obj_set_style_text_color(compactDate, lv_color_hex(0x666677), 0);
    lv_obj_set_style_text_font(compactDate, &Ubuntu_16px, 0);
    lv_obj_set_pos(compactDate, SCALED(250), SCALED_H(38));
    lv_obj_add_flag(compactDate, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(compactDate, LV_OBJ_FLAG_EVENT_BUBBLE);
    m_compactDateLabels.push_back(compactDate);
}

// ============================================
// App cell creation
// ============================================

void Launcher::createAppCell(lv_obj_t* pageObj, size_t appIdx, int x, int y, int cellHeight) {
    const auto& app = m_apps[appIdx];
    const int CELL_WIDTH = LAUNCHER_CELL_WIDTH;
    
    lv_obj_t* cell = lv_obj_create(pageObj);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, CELL_WIDTH, cellHeight);
    lv_obj_set_pos(cell, x, y);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    
    // Heap-allocate callback data (freed in release())
    auto* cellData = new CellData{this, appIdx};
    m_cellData.push_back(cellData);
    
    lv_obj_add_event_cb(cell, onTouchStartStatic, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(cell, onCellClickStatic, LV_EVENT_RELEASED, cellData);
    
    // Load icon
    lv_obj_t* iconWidget = loadIcon(cell, app, appIdx);
    if (iconWidget) {
        m_icons[appIdx] = iconWidget;
    }
    
    // Title
    lv_obj_t* label = lv_label_create(cell);
#ifdef LAUNCHER_TITLE_MAX_CHARS
    P::String displayTitle = truncateTitle(app.title, LAUNCHER_TITLE_MAX_CHARS);
    lv_label_set_text(label, displayTitle.c_str());
#else
    lv_label_set_text(label, app.title.c_str());
#endif
    lv_obj_set_style_text_color(label, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(label, &Ubuntu_16px, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, CELL_WIDTH);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, LAUNCHER_LABEL_TOP_OFFSET);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

// ============================================
// Page creation
// ============================================

void Launcher::createPage(lv_obj_t* scr, size_t pageIdx, size_t totalApps) {
    const int COLS = LAUNCHER_COLS;
    const int ROWS_PAGE1 = LAUNCHER_ROWS_PAGE1;
    const int ROWS_OTHER = LAUNCHER_ROWS_OTHER;
    const int APPS_PAGE1 = COLS * ROWS_PAGE1;
    const int APPS_OTHER = COLS * ROWS_OTHER;
    const int CELL_WIDTH = LAUNCHER_CELL_WIDTH;
    const int CELL_HEIGHT_PAGE1 = LAUNCHER_CELL_HEIGHT_P1;
    const int CELL_HEIGHT_OTHER = LAUNCHER_CELL_HEIGHT;
    const int PAGE_WIDTH = LAUNCHER_PAGE_WIDTH;
    const int PAGE_HEIGHT = LAUNCHER_PAGE_HEIGHT;
    
    lv_obj_t* pageObj = lv_obj_create(scr);
    lv_obj_remove_style_all(pageObj);
    lv_obj_set_size(pageObj, PAGE_WIDTH, PAGE_HEIGHT);
    lv_obj_align(pageObj, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(pageObj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(pageObj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(pageObj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(pageObj, LV_OBJ_FLAG_EVENT_BUBBLE);
    
    if (pageIdx > 0) {
        lv_obj_add_flag(pageObj, LV_OBJ_FLAG_HIDDEN);
    }
    m_pages.push_back(pageObj);
    
    int iconOffsetY = 0;
    
    if (pageIdx == 0) {
        createBigClock(pageObj);
        iconOffsetY = ICONS_START_Y_PAGE1;
    } else {
        createCompactClock(pageObj);
        iconOffsetY = ICONS_START_Y_OTHER;
    }
    
    // App cells
    int rows = (pageIdx == 0) ? ROWS_PAGE1 : ROWS_OTHER;
    int cellHeight = (pageIdx == 0) ? CELL_HEIGHT_PAGE1 : CELL_HEIGHT_OTHER;
    int gridWidth = COLS * CELL_WIDTH;
    int marginX = (PAGE_WIDTH - gridWidth) / 2;
    
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < COLS; col++) {
            size_t appIdx;
            if (pageIdx == 0) {
                appIdx = row * COLS + col;
            } else {
                appIdx = APPS_PAGE1 + (pageIdx - 1) * APPS_OTHER + row * COLS + col;
            }
            if (appIdx >= totalApps) break;
            
            int x = col * CELL_WIDTH + marginX;
            int y = row * cellHeight + iconOffsetY;
            
            createAppCell(pageObj, appIdx, x, y, cellHeight);
        }
    }
}

// ============================================
// Dots creation
// ============================================

void Launcher::createDots(lv_obj_t* scr, size_t numPages) {
    lv_obj_t* dots = lv_obj_create(scr);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, numPages * (DOT_SIZE + DOT_SPACING), DOT_SIZE + 6);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -DOTS_BOTTOM_OFFSET);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, DOT_SPACING, 0);
    m_dotsContainer = dots;
    
    if (!LAUNCHER_SHOW_DOTS_PAGE1) {
        lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    }
    
    for (size_t i = 0; i < numPages; i++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(dot, DOT_SIZE / 2, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        
        if (i == 0) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_shadow_color(dot, lv_color_hex(0xffffff), 0);
            lv_obj_set_style_shadow_width(dot, DOT_SIZE, 0);
            lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);
        } else {
            lv_obj_set_style_bg_color(dot, lv_color_hex(0x444455), 0);
        }
        m_dots.push_back(dot);
    }
}

// ============================================
// Public API
// ============================================

void Launcher::show(const std::vector<LauncherAppInfo>& apps) {
    LOG_I(Log::APP, "Launcher::show() - %d apps", (int)apps.size());
    
    // Store apps
    m_apps.clear();
    for (const auto& app : apps) {
        m_apps.push_back(app);
    }
    
    // Calculate pages
    const int COLS = LAUNCHER_COLS;
    const int ROWS_PAGE1 = LAUNCHER_ROWS_PAGE1;
    const int ROWS_OTHER = LAUNCHER_ROWS_OTHER;
    const int APPS_PAGE1 = COLS * ROWS_PAGE1;
    const int APPS_OTHER = COLS * ROWS_OTHER;
    
    size_t totalApps = m_apps.size();
    if (totalApps <= (size_t)APPS_PAGE1) {
        m_numPages = 1;
    } else {
        m_numPages = 1 + (totalApps - APPS_PAGE1 + APPS_OTHER - 1) / APPS_OTHER;
    }
    if (m_numPages < 1) m_numPages = 1;
    
    // Reset UI state (m_cellData freed via cleanup() before show())
    m_pages.clear();
    m_dots.clear();
    m_dotsContainer = nullptr;
    m_icons.clear();
    m_clockLabels.clear();
    m_bigDateLabels.clear();
    m_compactDayLabels.clear();
    m_compactDateLabels.clear();
    m_currentPage = 0;
    m_touchStartX = -1;
    
    m_icons.resize(m_apps.size(), nullptr);
    
    // Setup screen
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a12), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    
    // Gesture handler
    lv_obj_add_event_cb(scr, onGestureStatic, LV_EVENT_GESTURE, this);
    
    // Create pages
    for (size_t page = 0; page < m_numPages; page++) {
        createPage(scr, page, totalApps);
    }
    
    // Create dots
    if (m_numPages > 1) {
        createDots(scr, m_numPages);
    }
    
    // Clock timer
    if (m_clockTimer) {
        lv_timer_delete(m_clockTimer);
    }
    m_clockTimer = lv_timer_create(updateClocksStatic, 1000, this);
    
    LOG_I(Log::APP, "Launcher shown: %d apps, %d pages", (int)m_apps.size(), (int)m_numPages);
}

void Launcher::release() {
    if (m_clockTimer) {
        lv_timer_delete(m_clockTimer);
        m_clockTimer = nullptr;
    }
    
    // Free heap-allocated cell data
    for (auto* cd : m_cellData) {
        delete cd;
    }
    m_cellData.clear();
    
    m_apps.clear();
    m_pages.clear();
    m_dots.clear();
    m_dotsContainer = nullptr;
    m_icons.clear();
    m_clockLabels.clear();
    m_bigDateLabels.clear();
    m_compactDayLabels.clear();
    m_compactDateLabels.clear();
    m_currentPage = 0;
    m_numPages = 0;
    m_touchStartX = -1;
}

void Launcher::cleanup() {
    release();
}

} // namespace UI
