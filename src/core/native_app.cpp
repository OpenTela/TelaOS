/**
 * native_app.cpp — NativeApp implementation
 */

#include "core/native_app.h"
#include "core/app_manager.h"
#include <algorithm>

// ============ Registry ============

static std::vector<NativeApp*>& registry() {
    static std::vector<NativeApp*> s_apps;
    return s_apps;
}

void NativeApp::registerApp(NativeApp* app) {
    if (!app) return;
    registry().push_back(app);
}

NativeApp* NativeApp::find(const char* name) {
    if (!name) return nullptr;
    for (auto* app : registry()) {
        if (strcmp(app->name(), name) == 0) return app;
    }
    return nullptr;
}

const std::vector<NativeApp*>& NativeApp::all() {
    return registry();
}

// ============ Services ============

lv_obj_t* NativeApp::page() const {
    return lv_screen_active();
}

void NativeApp::goHome() {
    App::Manager::instance().returnToLauncher();
}
