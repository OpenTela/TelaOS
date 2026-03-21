#include "ui/dynamic_app.h"
#include "ui/ui_html_internal.h"
#include "core/core.h"
#include "core/state_store.h"
#include "utils/log_config.h"
#include <lvgl.h>
#include <algorithm>

static const char* TAG = "DynamicApp";

DynamicApp::~DynamicApp() {
    // Clean LVGL widgets FIRST — they reference iconPaths/imagePaths
    lv_obj_t* scr = lv_screen_active();
    if (scr) {
        lv_obj_clean(scr);
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    }
    // Then all vectors, strings, maps auto-destroyed by default destructors
}

int DynamicApp::findPage(const char* id) const {
    for (int i = 0; i < page_count; i++) {
        if (page_ids[i] == id) return i;
    }
    return INVALID_INDEX;
}

lv_obj_t* DynamicApp::findElement(const char* id) const {
    for (const auto& el : elements) {
        if (el->id == id) return el->obj();
    }
    return nullptr;
}

int DynamicApp::addElement(ElementDesc& d) {
    if (!d.id || !d.id[0]) return INVALID_INDEX;
    
    auto el = P::create<UI::Element>();
    el->id = d.id;
    el->w.handle = d.obj;
    _unique_id(d.obj, d.id);
    el->is_page = d.is_page;
    el->href     = (d.href     && d.href[0])     ? d.href     : "";
    el->onclick  = (d.onclick  && d.onclick[0])  ? d.onclick  : "";
    el->onchange = (d.onchange && d.onchange[0]) ? d.onchange : "";
    el->oninput  = (d.oninput  && d.oninput[0])  ? d.oninput  : "";
    el->bind     = (d.bind     && d.bind[0])     ? d.bind     : "";
    el->tpl      = (d.tpl && strchr(d.tpl, '{')) ? d.tpl : "";
    el->classTemplate = (d.classTpl && strchr(d.classTpl, '{')) ? d.classTpl : "";
    
    // Parse visibleBind
    if (d.visibleBind && d.visibleBind[0]) {
        el->visibleBind = extractBindVar(d.visibleBind);
        if (!el->visibleBind.empty()) {
            P::String val = g_core.store().getString(el->visibleBind);
            bool visible = (val == "true" || val == "1");
            if (!visible) lv_obj_add_flag(d.obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    // Parse bgcolorBind
    if (d.bgcolorBind && d.bgcolorBind[0]) {
        el->bgcolorBind = extractBindVar(d.bgcolorBind);
        LOG_D(Log::UI, "store_element: id=%s bgcolorBind=%s (from %s)", d.id, el->bgcolorBind.c_str(), d.bgcolorBind);
    }
    
    // Parse colorBind
    if (d.colorBind && d.colorBind[0]) {
        el->colorBind = extractBindVar(d.colorBind);
    }
    
    int idx = (int)elements.size();
    elements.push_back(std::move(el));
    
    if (d.zIndex != 0 && idx >= 0) {
        elements[idx]->zIndex = d.zIndex;
    }
    
    // Apply deferred CSS z-index
    if (!deferredZIndex.empty() && d.obj) {
        auto it = deferredZIndex.find(d.obj);
        if (it != deferredZIndex.end()) {
            elements[idx]->zIndex = it->second;
            deferredZIndex.erase(it);
        }
    }
    
    return idx;
}

int DynamicApp::addPage(const char* id, lv_obj_t* obj) {
    ElementDesc d = {};
    d.id = id;
    d.obj = obj;
    d.is_page = true;
    return addElement(d);
}

void DynamicApp::setZIndex(lv_obj_t* handle, int z) {
    for (auto& el : elements) {
        if (el->w.handle == handle || el->parentObj == handle) {
            el->zIndex = z;
            return;
        }
    }
    deferredZIndex[handle] = z;
}

void DynamicApp::applyZIndex() {
    struct ZEntry { lv_obj_t* obj; int z; };
    P::Array<ZEntry> negatives, positives;
    
    for (const auto& el : elements) {
        if (el->zIndex == 0 || el->is_page) continue;
        lv_obj_t* target = el->parentObj ? el->parentObj : el->w.handle;
        if (!target) continue;
        
        if (el->zIndex < 0) negatives.push_back({target, el->zIndex});
        else                 positives.push_back({target, el->zIndex});
    }
    
    std::sort(negatives.begin(), negatives.end(), [](const ZEntry& a, const ZEntry& b) {
        return a.z > b.z;
    });
    for (auto& e : negatives) lv_obj_move_to_index(e.obj, 0);
    
    std::sort(positives.begin(), positives.end(), [](const ZEntry& a, const ZEntry& b) {
        return a.z < b.z;
    });
    for (auto& e : positives) lv_obj_move_foreground(e.obj);
}
