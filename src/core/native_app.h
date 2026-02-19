#pragma once
/**
 * native_app.h — Base class for native C++ applications
 *
 * Native apps are compiled into firmware. They use the same widget
 * system as HTML apps but with compile-time type safety.
 *
 * Usage:
 *   class MyApp : public NativeApp {
 *   public:
 *       MyApp() : NativeApp("myapp", "My App", &icon_64px) {}
 *
 *       Label title = { .text = "Hello", .font = 48, .align = center };
 *
 *       void onCreate() override {
 *           ui::build(page(), 0x000000, title);
 *       }
 *   };
 *   REGISTER_NATIVE_APP(MyApp);
 */

#include <lvgl.h>
#include <vector>
#include "widgets/widgets.h"

class NativeApp {
public:
    NativeApp(const char* name, const char* title, const char* icon = nullptr)
        : m_name(name), m_title(title), m_icon(icon) {}

    virtual ~NativeApp() = default;

    // ---- Lifecycle (override these) ----

    /// Called when app is launched. Create UI here.
    virtual void onCreate() {}

    /// Called when app is closed. Cleanup if needed.
    virtual void onDestroy() {}

    /// Called periodically (~1s) while app is running.
    virtual void onTick() {}

    // ---- App info ----

    const char* name()  const { return m_name; }
    const char* title() const { return m_title; }
    const char* icon()  const { return m_icon; }

    // ---- Services ----

    /// Root LVGL object for this app's UI
    lv_obj_t* page() const;

    /// Return to launcher
    void goHome();

    // ---- Registry (static) ----

    static void registerApp(NativeApp* app);
    static NativeApp* find(const char* name);
    static const std::vector<NativeApp*>& all();

private:
    const char* m_name;
    const char* m_title;
    const char* m_icon;
};

// ---- Auto-registration macro ----

#define REGISTER_NATIVE_APP(ClassName)                                 \
    static ClassName _native_instance_##ClassName;                     \
    static struct _NativeReg_##ClassName {                             \
        _NativeReg_##ClassName() {                                     \
            NativeApp::registerApp(&_native_instance_##ClassName);     \
        }                                                              \
    } _native_autoreg_##ClassName;
