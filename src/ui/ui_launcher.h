#pragma once
/**
 * ui_launcher.h - Launcher UI class
 */

#include <lvgl.h>
#include <vector>
#include "utils/psram_alloc.h"

namespace UI {

/// App info needed for launcher display
struct LauncherAppInfo {
    P::String name;
    P::String title;
    P::String category;
    P::String iconPath;
    
    // Pre-decoded icon
    const lv_image_dsc_t* iconDsc = nullptr;
    bool hasIcon = false;
};

/// Launcher UI - displays app grid with pages and clock
class Launcher {
public:
    ~Launcher() { release(); }
    
    /// Show launcher with given apps
    void show(const std::vector<LauncherAppInfo>& apps);
    
    /// Cleanup resources (call before launching app)
    void cleanup();
    
private:
    // Time helpers
    const char* getTimeStr();
    const char* getDayName();
    const char* getDayNameLower();
    const char* getDateLower();
    int getDay();
    const char* getMonthShort();
    
    // Title truncation (UTF-8 aware)
    P::String truncateTitle(const P::String& title, size_t maxChars);
    
    // Icon loading - returns created icon widget or nullptr
    lv_obj_t* loadIcon(lv_obj_t* cell, const LauncherAppInfo& app, size_t appIdx);
    
    // UI creation
    void createPage(lv_obj_t* scr, size_t pageIdx, size_t totalApps);
    void createAppCell(lv_obj_t* page, size_t appIdx, int x, int y, int cellHeight);
    void createDots(lv_obj_t* scr, size_t numPages);
    void createBigClock(lv_obj_t* page);
    void createCompactClock(lv_obj_t* page);
    
    // Callbacks (static, access instance via user_data)
    static void onGestureStatic(lv_event_t* e);
    static void onTouchStartStatic(lv_event_t* e);
    static void onCellClickStatic(lv_event_t* e);
    static void updateClocksStatic(lv_timer_t* t);
    
    // Instance methods for callbacks
    void onGesture(lv_dir_t dir);
    void onTouchStart(int16_t x, int16_t y);
    void onCellClick(size_t appIdx, uint32_t duration);
    void updateClocks();
    
    // Private cleanup helper (called from cleanup() and destructor)
    void release();
    
    // State
    P::Array<LauncherAppInfo> m_apps;
    P::Array<lv_obj_t*> m_pages;
    P::Array<lv_obj_t*> m_dots;
    P::Array<lv_obj_t*> m_icons;
    P::Array<lv_obj_t*> m_clockLabels;
    lv_obj_t* m_dotsContainer = nullptr;
    lv_timer_t* m_clockTimer = nullptr;
    size_t m_currentPage = 0;
    size_t m_numPages = 0;
    
    // Touch tracking
    int16_t m_touchStartX = -1;
    int16_t m_touchStartY = -1;
    uint32_t m_touchStartTime = 0;
    
    // Cell callback data (heap allocated, pointers stable)
    struct CellData { Launcher* self; size_t appIdx; };
    P::Array<CellData*> m_cellData;
    
    // Time tracking
    uint32_t m_startMillis = 0;
    bool m_timeInitialized = false;
};

} // namespace UI
