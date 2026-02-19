/**
 * ui_shade.cpp — System shade implementation
 *
 * Swipe-down overlay on lv_layer_top().
 * Two toggles: Bluetooth, Auto-off (screen dimming + sleep).
 * Inactivity: nudge at 30s, dim at 45s, sleep at 60s.
 *
 * NOTE: Shade does NOT handle its own touch triggering.
 * It is opened via Shade::open() from:
 *   - app_manager.cpp  (short swipe from top edge)
 *   - ui_launcher.cpp  (swipe down gesture)
 */

#include "ui/ui_shade.h"
#include "hal/display_hal.h"
#include "ble/ble_bridge.h"
#include "core/app_manager.h"
#include "utils/log_config.h"
#include "utils/font.h"
#include <Arduino.h>

static const char* TAG = "Shade";

// ============================================
// Config
// ============================================
static const int SHADE_H         = 180;
static const int ANIM_MS         = 250;
static const uint8_t MAX_BRIGHTNESS = 255;
static const uint8_t NUDGE_BRIGHTNESS = 230; // ~90%
static const uint8_t DIM_BRIGHTNESS   = 90;  // ~35%

// Loaded from config on init
static int s_nudgeTimeout = 30;
static int s_dimTimeout   = 45;
static int s_sleepTimeout = 60;

static const uint32_t BG_COLOR      = 0x1a1a2e;
static const uint32_t BT_ON_COLOR   = 0x1565C0;
static const uint32_t BT_OFF_COLOR  = 0x333333;
static const uint32_t AUTO_ON_COLOR = 0x2E7D32;
static const uint32_t AUTO_OFF_COLOR= 0x333333;
static const uint32_t SCRIM_COLOR   = 0x000000;

// ============================================
// State
// ============================================
static bool s_initialized = false;
static bool s_open        = false;
static bool s_autoOff     = true;
static uint8_t s_userBrightness = MAX_BRIGHTNESS;

enum InactState { Active, Nudged, Dimmed, Sleeping };
static InactState s_inactState = Active;

static lv_obj_t* s_scrim      = nullptr;
static lv_obj_t* s_panel      = nullptr;
static lv_obj_t* s_btnBT      = nullptr;
static lv_obj_t* s_btnAutoOff = nullptr;
static lv_obj_t* s_blocker    = nullptr;    // invisible touch eater when dimmed/sleeping
static lv_timer_t* s_inactTimer = nullptr;

// ============================================
// Forward declarations
// ============================================
static void createUI();
static void updateBTButton();
static void updateAutoOffButton();
static void onScrimClick(lv_event_t* e);
static void onBlockerClick(lv_event_t* e);
static void onBTClick(lv_event_t* e);
static void onAutoOffClick(lv_event_t* e);
static void onInactivityTimer(lv_timer_t* t);
static void animY(lv_obj_t* obj, int32_t start, int32_t end, uint32_t ms);
static void restoreBrightness();
static void showBlocker();
static void hideBlocker();

// ============================================
// Public API
// ============================================

void Shade::init() {
    if (s_initialized) return;

    createUI();
    s_inactTimer = lv_timer_create(onInactivityTimer, 1000, nullptr);
    s_initialized = true;
    LOG_I(Log::UI, "Shade UI created");
}

void Shade::applyConfig() {
    auto& cfg = App::Manager::instance().systemConfig;
    s_autoOff        = cfg.getBool("power.auto_sleep");
    s_userBrightness = (uint8_t)cfg.getInt("display.brightness");
    s_nudgeTimeout   = cfg.getInt("power.nudge_timeout");
    s_dimTimeout     = cfg.getInt("power.dim_timeout");
    s_sleepTimeout   = cfg.getInt("power.sleep_timeout");

    if (s_userBrightness > 0) {
        display_set_brightness(s_userBrightness);
    }

    // Autostart BLE if configured
    if (cfg.getBool("bluetooth.enabled") && !BLEBridge::isInitialized()) {
        BLEBridge::init();
        LOG_I(Log::BLE, "BLE enabled from config");
    }

    updateAutoOffButton();
    updateBTButton();
    LOG_I(Log::UI, "Shade config applied (auto_sleep=%d, brightness=%d)", s_autoOff, s_userBrightness);
}

void Shade::open() {
    if (s_open) return;
    s_open = true;
    restoreBrightness();

    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_scrim, LV_OPA_TRANSP, 0);

    updateBTButton();
    updateAutoOffButton();

    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    animY(s_panel, -SHADE_H, 0, ANIM_MS);

    // Animate scrim fade in
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_scrim);
    lv_anim_set_values(&a, 0, 180);
    lv_anim_set_time(&a, ANIM_MS);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)obj, v, 0);
    });
    lv_anim_start(&a);
    LOG_D(Log::UI, "Shade opened");
}

void Shade::close() {
    if (!s_open) return;
    s_open = false;

    animY(s_panel, 0, -SHADE_H, ANIM_MS);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_scrim);
    lv_anim_set_values(&a, 180, 0);
    lv_anim_set_time(&a, ANIM_MS);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)obj, v, 0);
    });
    lv_anim_set_completed_cb(&a, [](lv_anim_t* a) {
        lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
    });
    lv_anim_start(&a);
    LOG_D(Log::UI, "Shade closed");
}

bool Shade::isOpen() { return s_open; }

void Shade::setAutoOff(bool enabled) {
    s_autoOff = enabled;
    if (!enabled) restoreBrightness();
    updateAutoOffButton();
    App::Manager::instance().systemConfig.set("power.auto_sleep", enabled);
    LOG_I(Log::UI, "Auto-off: %s", enabled ? "ON" : "OFF");
}

bool Shade::autoOffEnabled() { return s_autoOff; }

// ============================================
// UI Construction
// ============================================

static lv_obj_t* createToggleBtn(lv_obj_t* parent, const char* text,
                                  lv_event_cb_t cb, int x_ofs) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 120, 90);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, x_ofs, 50);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &Ubuntu_16px, 0);
    lv_obj_center(lbl);

    return btn;
}

static void createUI() {
    lv_obj_t* layer = lv_layer_top();

    // Touch blocker — invisible full-screen overlay when dimmed/sleeping.
    // Eats all taps so they don't reach widgets below.
    // Created first = lowest z-order on layer_top (below scrim & panel).
    s_blocker = lv_obj_create(layer);
    lv_obj_set_size(s_blocker, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_blocker, 0, 0);
    lv_obj_set_style_opa(s_blocker, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_blocker, 0, 0);
    lv_obj_set_style_pad_all(s_blocker, 0, 0);
    lv_obj_remove_flag(s_blocker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_blocker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_blocker, onBlockerClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_blocker, LV_OBJ_FLAG_HIDDEN);

    // Scrim (dark overlay, catches taps to close)
    s_scrim = lv_obj_create(layer);
    lv_obj_set_size(s_scrim, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(s_scrim, 0, 0);
    lv_obj_set_style_bg_color(s_scrim, lv_color_hex(SCRIM_COLOR), 0);
    lv_obj_set_style_bg_opa(s_scrim, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_scrim, 0, 0);
    lv_obj_set_style_radius(s_scrim, 0, 0);
    lv_obj_set_style_pad_all(s_scrim, 0, 0);
    lv_obj_remove_flag(s_scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_scrim, onScrimClick, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(s_scrim, LV_OBJ_FLAG_HIDDEN);

    // Shade panel
    s_panel = lv_obj_create(layer);
    lv_obj_set_size(s_panel, SCREEN_WIDTH, SHADE_H);
    lv_obj_set_pos(s_panel, 0, -SHADE_H);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(BG_COLOR), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_radius(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);

    // Handle bar
    lv_obj_t* handle = lv_obj_create(s_panel);
    lv_obj_set_size(handle, 40, 4);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(handle, lv_color_hex(0x555555), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(handle, 2, 0);
    lv_obj_set_style_border_width(handle, 0, 0);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_CLICKABLE);

    // Toggle buttons
    s_btnBT = createToggleBtn(s_panel, "BT", onBTClick, -70);
    s_btnAutoOff = createToggleBtn(s_panel, "Auto\nOff", onAutoOffClick, 70);

    updateBTButton();
    updateAutoOffButton();
}

// ============================================
// Button state updates
// ============================================

static void updateBTButton() {
    bool on = BLEBridge::isInitialized();
    lv_obj_set_style_bg_color(s_btnBT, lv_color_hex(on ? BT_ON_COLOR : BT_OFF_COLOR), 0);
    lv_obj_set_style_bg_opa(s_btnBT, LV_OPA_COVER, 0);
}

static void updateAutoOffButton() {
    lv_obj_set_style_bg_color(s_btnAutoOff,
        lv_color_hex(s_autoOff ? AUTO_ON_COLOR : AUTO_OFF_COLOR), 0);
    lv_obj_set_style_bg_opa(s_btnAutoOff, LV_OPA_COVER, 0);
}

// ============================================
// Event handlers
// ============================================

static void onScrimClick(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_active();
    if (indev) {
        lv_point_t p;
        lv_indev_get_point(indev, &p);
        if (p.y > SHADE_H) Shade::close();
    }
}

static void onBTClick(lv_event_t* e) {
    if (BLEBridge::isInitialized()) {
        BLEBridge::deinit();
        LOG_I(Log::BLE, "BT toggled OFF from shade");
    } else {
        BLEBridge::init();
        LOG_I(Log::BLE, "BT toggled ON from shade");
    }
    updateBTButton();
    App::Manager::instance().systemConfig.set("bluetooth.enabled", BLEBridge::isInitialized());
}

static void onAutoOffClick(lv_event_t* e) {
    Shade::setAutoOff(!s_autoOff);
}

// ============================================
// Touch blocker (dimmed/sleeping wake tap)
// ============================================

static void onBlockerClick(lv_event_t* e) {
    LOG_D(Log::UI, "Wake tap (blocked)");
    restoreBrightness();
}

static void showBlocker() {
    if (s_blocker) lv_obj_remove_flag(s_blocker, LV_OBJ_FLAG_HIDDEN);
}

static void hideBlocker() {
    if (s_blocker) lv_obj_add_flag(s_blocker, LV_OBJ_FLAG_HIDDEN);
}

// ============================================
// Inactivity tracker
// ============================================

static void restoreBrightness() {
    if (s_inactState == Sleeping) {
        display_wake();
    } else if (s_inactState != Active) {
        display_set_brightness(s_userBrightness);
    }
    s_inactState = Active;
    hideBlocker();
}

static void onInactivityTimer(lv_timer_t* t) {
    if (!s_autoOff || s_open) return;

    uint32_t inactMs = lv_display_get_inactive_time(lv_display_get_default());
    uint32_t inactS = inactMs / 1000;

    if (inactS >= (uint32_t)s_sleepTimeout) {
        if (s_inactState != Sleeping) {
            LOG_I(Log::UI, "Inactivity %us — sleep", inactS);
            display_sleep();
            s_inactState = Sleeping;
            showBlocker();
        }
    } else if (inactS >= (uint32_t)s_dimTimeout) {
        if (s_inactState != Dimmed) {
            LOG_D(Log::UI, "Inactivity %us — dim", inactS);
            display_set_brightness(DIM_BRIGHTNESS);
            s_inactState = Dimmed;
            showBlocker();
        }
    } else if (inactS >= (uint32_t)s_nudgeTimeout) {
        if (s_inactState != Nudged) {
            LOG_D(Log::UI, "Inactivity %us — nudge", inactS);
            display_set_brightness(NUDGE_BRIGHTNESS);
            s_inactState = Nudged;
            showBlocker();
        }
    } else {
        restoreBrightness();
    }
}

// ============================================
// Animation helper
// ============================================

static void animY(lv_obj_t* obj, int32_t start, int32_t end, uint32_t ms) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, start, end);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_y((lv_obj_t*)obj, v);
    });
    lv_anim_start(&a);
}
