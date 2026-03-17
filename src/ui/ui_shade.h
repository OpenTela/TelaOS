#pragma once
/**
 * ui_shade.h — System shade (swipe-down overlay)
 *
 * Features:
 *   - Swipe down from top edge to open
 *   - Toggle buttons: BT, Auto-off
 *   - Inactivity tracker: nudge@30s, dim@45s, sleep@60s
 */

#include <lvgl.h>
#include <cstdint>

class Shade {
public:
    static Shade& instance();
    
    void init();
    void applyConfig();
    void open();
    void close();
    bool isOpen() const { return m_open; }
    
    void setAutoOff(bool enabled);
    bool autoOffEnabled() const { return m_autoOff; }

private:
    Shade() = default;
    
    void createUI();
    void updateBTButton();
    void updateAutoOffButton();
    void restoreBrightness();
    void showBlocker();
    void hideBlocker();
    
    static void onScrimClick(lv_event_t* e);
    static void onBlockerClick(lv_event_t* e);
    static void onBTClick(lv_event_t* e);
    static void onAutoOffClick(lv_event_t* e);
    static void onInactivityTimer(lv_timer_t* t);
    static void animY(lv_obj_t* obj, int32_t start, int32_t end, uint32_t ms);

    enum InactState { Active, Nudged, Dimmed, Sleeping };
    
    // Config
    int      m_nudgeTimeout = 30;
    int      m_dimTimeout   = 45;
    int      m_sleepTimeout = 60;
    
    // State
    bool     m_initialized = false;
    bool     m_open        = false;
    bool     m_autoOff     = true;
    uint8_t  m_userBrightness = 255;
    InactState m_inactState = Active;
    
    // LVGL objects
    lv_obj_t*   m_scrim      = nullptr;
    lv_obj_t*   m_panel      = nullptr;
    lv_obj_t*   m_btnBT      = nullptr;
    lv_obj_t*   m_btnAutoOff = nullptr;
    lv_obj_t*   m_blocker    = nullptr;
    lv_timer_t* m_inactTimer = nullptr;
};
