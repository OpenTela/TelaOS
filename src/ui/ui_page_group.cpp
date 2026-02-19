/**
 * ui_page_group.cpp - PageGroup methods for tileview-based swipeable page groups
 * 
 * Extracted from ui_html.cpp render_internal.
 */

#include "lvgl.h"
#include "ui/ui_engine.h"
#include "ui/ui_html_internal.h"
#include "utils/log_config.h"

static const char* TAG = "ui_page_group";

namespace {

namespace DotIndicator {
    constexpr int SIZE = 8;
    constexpr int SPACING = 8;
    constexpr int THICKNESS = 12;
    constexpr int CELL_SIZE = 16;
    constexpr int MARGIN = 15;
}

} // anonymous namespace

// Tileview value changed callback - update indicator dots
static void tileview_changed_cb(lv_event_t* e) {
    lv_obj_t* tv = (lv_obj_t*)lv_event_get_target(e);
    int grp_idx = (int)(intptr_t)lv_event_get_user_data(e);
    
    if (grp_idx < 0 || grp_idx >= (int)groups.size()) return;
    
    UI::PageGroup* grp = &groups[grp_idx];
    
    lv_obj_t* active_tile = lv_tileview_get_tile_act(tv);
    int new_idx = 0;
    for (int i = 0; i < (int)grp->page_ids.size(); i++) {
        if (grp->page_objs[i] == active_tile) {
            new_idx = i;
            break;
        }
    }
    
    grp->current_page_idx = new_idx;
    grp->updateIndicator(new_idx);
    LOG_I(Log::UI, "Swipe: %s -> page %d", grp->id.c_str(), new_idx);
}

namespace UI {

void PageGroup::create(lv_obj_t* parent) {
    screen = parent;
    tileview = lv_tileview_create(parent);
    lv_obj_set_size(tileview, lv_pct(FULL_SIZE_PCT), lv_pct(FULL_SIZE_PCT));
    lv_obj_set_pos(tileview, 0, 0);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    
    if (indicator == IndicatorType::Scrollbar) {
        lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_ACTIVE);
    } else {
        lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    }
    
    current_page_idx = 0;
}

lv_obj_t* PageGroup::addTile(const P::String& pageId) {
    int col = (int)page_ids.size();
    
    // Determine swipe directions
    int dirs_int = 0;
    if (isHorizontal()) {
        if (col > 0) dirs_int |= LV_DIR_LEFT;
        dirs_int |= LV_DIR_RIGHT;
    } else {
        if (col > 0) dirs_int |= LV_DIR_TOP;
        dirs_int |= LV_DIR_BOTTOM;
    }
    
    lv_obj_t* tile;
    if (isHorizontal()) {
        tile = lv_tileview_add_tile(tileview, col, 0, (lv_dir_t)dirs_int);
    } else {
        tile = lv_tileview_add_tile(tileview, 0, col, (lv_dir_t)dirs_int);
    }
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);
    
    page_ids.push_back(pageId);
    page_objs.push_back(tile);
    
    return tile;
}

static void createDots(PageGroup& grp, lv_obj_t* screenParent) {
    if (grp.indicator != IndicatorType::Dots) return;
    if ((int)grp.page_ids.size() <= 1) return;
    
    lv_obj_t* ind = lv_obj_create(screenParent);
    lv_obj_set_style_bg_opa(ind, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ind, 0, 0);
    lv_obj_set_style_pad_all(ind, 0, 0);
    lv_obj_clear_flag(ind, LV_OBJ_FLAG_SCROLLABLE);
    
    int count = (int)grp.page_ids.size();
    
    if (grp.isHorizontal()) {
        lv_obj_set_size(ind, count * DotIndicator::CELL_SIZE, DotIndicator::THICKNESS);
        lv_obj_align(ind, LV_ALIGN_BOTTOM_MID, 0, -DotIndicator::MARGIN);
        lv_obj_set_flex_flow(ind, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(ind, DotIndicator::SPACING, 0);
    } else {
        lv_obj_set_size(ind, DotIndicator::THICKNESS, count * DotIndicator::CELL_SIZE);
        lv_obj_align(ind, LV_ALIGN_RIGHT_MID, -DotIndicator::SPACING, 0);
        lv_obj_set_flex_flow(ind, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(ind, DotIndicator::SPACING, 0);
    }
    lv_obj_set_flex_align(ind, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_move_foreground(ind);
    
    for (int i = 0; i < count; i++) {
        lv_obj_t* dot = lv_obj_create(ind);
        lv_obj_set_size(dot, DotIndicator::SIZE, DotIndicator::SIZE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(i == 0 ? 0xFFFFFF : 0x666666), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    grp.indicator_obj = ind;
}

void PageGroup::updateIndicator(int activeIdx) {
    if (!indicator_obj) return;
    
    uint32_t child_cnt = lv_obj_get_child_cnt(indicator_obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t* dot = lv_obj_get_child(indicator_obj, i);
        lv_obj_set_style_bg_color(dot, 
            lv_color_hex((int)i == activeIdx ? 0xFFFFFF : 0x666666), 0);
    }
}

void PageGroup::finalize(int grpIdx) {
    createDots(*this, screen);
    
    lv_obj_add_event_cb(tileview, ::tileview_changed_cb, LV_EVENT_VALUE_CHANGED,
                        (void*)(intptr_t)grpIdx);
}

void PageGroup::hide() {
    if (tileview) {
        lv_obj_add_flag(tileview, LV_OBJ_FLAG_HIDDEN);
    }
    if (indicator_obj) {
        lv_obj_add_flag(indicator_obj, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace UI
