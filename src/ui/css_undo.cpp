/**
 * css_undo.cpp — what applying CSS changed on an object (see css_undo.h).
 *
 * The local styles are read before and after CSS; what differs is CSS's. LVGL 9.2 keeps an
 * object's local styles as lv_style_t entries per selector: they are read in place (the mock
 * LVGL has no such entries: there they are read property by property).
 */
#include "ui/css_undo.h"

#if !defined(LVGL_MOCK_ENABLED)
#include "src/core/lv_obj_private.h"
#include "src/core/lv_obj_style_private.h"
#endif

#include <cstring>

namespace UI {
namespace {

struct Local {
    lv_style_selector_t sel;
    lv_style_prop_t prop;
    lv_style_value_t value;
};

// The flags CSS sets: display: none, overflow, position, visibility: hidden (not tappable)
constexpr uint32_t kFlags[] = {LV_OBJ_FLAG_HIDDEN,        LV_OBJ_FLAG_SCROLLABLE, LV_OBJ_FLAG_OVERFLOW_VISIBLE,
                               LV_OBJ_FLAG_IGNORE_LAYOUT, LV_OBJ_FLAG_FLOATING,   LV_OBJ_FLAG_CLICKABLE};

uint32_t flagsOf(lv_obj_t* o) {
    uint32_t f = 0;
    for (uint32_t flag : kFlags) {
        if (lv_obj_has_flag(o, static_cast<lv_obj_flag_t>(flag))) f |= flag;
    }
    return f;
}

void setFlag(lv_obj_t* o, uint32_t flag, bool on) {
    if (on) lv_obj_add_flag(o, static_cast<lv_obj_flag_t>(flag));
    else lv_obj_clear_flag(o, static_cast<lv_obj_flag_t>(flag));
}

bool same(const lv_style_value_t& a, const lv_style_value_t& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

// f(selector, prop, value) for each local style property of o, of every part and state
template <typename F>
void forEachLocal(lv_obj_t* o, F f) {
#if defined(LVGL_MOCK_ENABLED)
    static const lv_style_selector_t kSelectors[] = {LV_PART_MAIN,      LV_STATE_PRESSED,  LV_STATE_FOCUSED,
                                                     LV_STATE_CHECKED,  LV_STATE_DISABLED, LV_PART_TEXTAREA_PLACEHOLDER};
    for (lv_style_selector_t sel : kSelectors) {
        for (int p = 1; p <= LV_STYLE_LAST_BUILT_IN_PROP; ++p) {
            lv_style_value_t v{};
            const auto prop = static_cast<lv_style_prop_t>(p);
            if (lv_obj_get_local_style_prop(o, prop, &v, sel) == LV_STYLE_RES_FOUND) f(sel, prop, v);
        }
    }
#else
    for (uint32_t i = 0; i < o->style_cnt; ++i) {
        const lv_obj_style_t& s = o->styles[i];
        if (!s.is_local || s.is_trans || !s.style) continue;
        const lv_style_t* st = s.style;
        if (!st->values_and_props || st->prop_cnt == 0 || st->prop_cnt == 255) continue;  // 255: a constant style
        const auto* values = static_cast<const lv_style_value_t*>(st->values_and_props);
        const auto* props = static_cast<const uint8_t*>(st->values_and_props) + st->prop_cnt * sizeof(lv_style_value_t);
        for (uint32_t k = 0; k < st->prop_cnt; ++k) f(static_cast<lv_style_selector_t>(s.selector), props[k], values[k]);
    }
#endif
}

// Between begin() and end(): the object as it was before CSS
P::Array<Local>& scratch() {
    static P::Array<Local> s_locals;
    return s_locals;
}
uint32_t s_flags = 0;
uint8_t s_scrollbar = 0, s_scrollDir = 0, s_longMode = 0;

uint8_t scrollbarOf(lv_obj_t* o) { return static_cast<uint8_t>(lv_obj_get_scrollbar_mode(o)); }
uint8_t scrollDirOf(lv_obj_t* o) { return static_cast<uint8_t>(lv_obj_get_scroll_dir(o)); }
uint8_t longModeOf(lv_obj_t* o) {
    return lv_obj_check_type(o, &lv_label_class) ? static_cast<uint8_t>(lv_label_get_long_mode(o)) : 0;
}

void setModes(lv_obj_t* o, uint8_t which, const uint8_t* scrollbar, const uint8_t* dir, const uint8_t* longMode) {
    if (which & 1) lv_obj_set_scrollbar_mode(o, static_cast<decltype(lv_obj_get_scrollbar_mode(o))>(*scrollbar));
    if (which & 2) lv_obj_set_scroll_dir(o, static_cast<lv_dir_t>(*dir));
    if (which & 4) lv_label_set_long_mode(o, static_cast<lv_label_long_mode_t>(*longMode));
}

}  // namespace

void CssUndo::begin(lv_obj_t* o) {
    P::Array<Local>& before = scratch();
    before.clear();
    if (!o) return;
    forEachLocal(o, [&](lv_style_selector_t sel, lv_style_prop_t prop, const lv_style_value_t& v) {
        before.push_back(Local{sel, prop, v});
    });
    s_flags = flagsOf(o);
    s_scrollbar = scrollbarOf(o);
    s_scrollDir = scrollDirOf(o);
    s_longMode = longModeOf(o);
}

void CssUndo::end(lv_obj_t* o) {
    if (!o) return;
    const P::Array<Local>& before = scratch();
    forEachLocal(o, [&](lv_style_selector_t sel, lv_style_prop_t prop, const lv_style_value_t& v) {
        const Local* was = nullptr;
        for (const Local& l : before) {
            if (l.sel == sel && l.prop == prop) {
                was = &l;
                break;
            }
        }
        if (was && same(was->value, v)) return;  // CSS left it as it was
        Prop p;
        p.sel = sel;
        p.prop = prop;
        p.had = was != nullptr;
        if (was) p.before = was->value;
        p.after = v;
        props_.push_back(p);
    });
    const uint32_t now = flagsOf(o);
    flags_ = now ^ s_flags;
    flagsBefore_ = s_flags & flags_;
    flagsAfter_ = now & flags_;
    const uint8_t modes[3][2] = {{s_scrollbar, scrollbarOf(o)}, {s_scrollDir, scrollDirOf(o)}, {s_longMode, longModeOf(o)}};
    uint8_t* const keep[3] = {scrollbar_, scrollDir_, longMode_};
    for (int k = 0; k < 3; ++k) {
        if (modes[k][0] == modes[k][1]) continue;
        modes_ |= static_cast<uint8_t>(1u << k);
        keep[k][0] = modes[k][0];
        keep[k][1] = modes[k][1];
    }
    props_.shrink_to_fit();  // kept for as long as the app lives: no room to spare
    scratch().clear();
}

void CssUndo::undo(lv_obj_t* o, CssUndo& kept) const {
    if (!o) return;
    for (const Prop& p : props_) {
        lv_style_value_t now{};
        if (lv_obj_get_local_style_prop(o, p.prop, &now, p.sel) != LV_STYLE_RES_FOUND) continue;  // taken off since
        if (same(now, p.after)) {  // still CSS's
            if (p.had) lv_obj_set_local_style_prop(o, p.prop, p.before, p.sel);
            else lv_obj_remove_local_style_prop(o, p.prop, p.sel);
        } else {  // set since by something else: it stays theirs
            Prop k = p;
            k.after = now;
            kept.props_.push_back(k);
        }
    }
    const uint32_t now = flagsOf(o);
    for (uint32_t flag : kFlags) {
        if (!(flags_ & flag)) continue;
        if ((now & flag) == (flagsAfter_ & flag)) {
            setFlag(o, flag, (flagsBefore_ & flag) != 0);
        } else {
            kept.flags_ |= flag;
            kept.flagsAfter_ |= now & flag;
        }
    }
    const uint8_t current[3] = {scrollbarOf(o), scrollDirOf(o), longModeOf(o)};
    const uint8_t* const mine[3] = {scrollbar_, scrollDir_, longMode_};
    uint8_t* const theirs[3] = {kept.scrollbar_, kept.scrollDir_, kept.longMode_};
    for (int k = 0; k < 3; ++k) {
        const uint8_t bit = static_cast<uint8_t>(1u << k);
        if (!(modes_ & bit)) continue;
        if (current[k] == mine[k][1]) {
            setModes(o, bit, &mine[0][0], &mine[1][0], &mine[2][0]);
        } else {
            kept.modes_ |= bit;
            theirs[k][1] = current[k];
        }
    }
}

void CssUndo::restore(lv_obj_t* o) const {
    if (!o) return;
    for (const Prop& p : props_) lv_obj_set_local_style_prop(o, p.prop, p.after, p.sel);
    for (uint32_t flag : kFlags) {
        if (flags_ & flag) setFlag(o, flag, (flagsAfter_ & flag) != 0);
    }
    setModes(o, modes_, &scrollbar_[1], &scrollDir_[1], &longMode_[1]);
}

}  // namespace UI
