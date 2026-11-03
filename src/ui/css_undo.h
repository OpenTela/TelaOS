#pragma once
/**
 * css_undo.h — what applying CSS changed on an object, kept to put it back. When a script changes
 * an element's class, the rules that stop matching give back what was there before them (UI::Dom).
 *
 * Kept: the object's local style properties of every part and state, the flags CSS sets
 * (display, overflow, position, visibility) and the scroll and long modes (overflow, white-space).
 * What something else set after CSS (an attribute, a binding, a script) is not CSS's any more:
 * undo() leaves it, and it goes back on top of the CSS that comes next.
 */

#include "lvgl.h"
#include "utils/psram_alloc.h"

#include <cstdint>

namespace UI {

class CssUndo {
public:
    // Around applying CSS to o: what is different after it is kept, with what was there before
    void begin(lv_obj_t* o);
    void end(lv_obj_t* o);
    bool empty() const { return props_.empty() && !flags_ && !modes_; }

    // What CSS changed and nothing changed since goes back to what was there before it. What
    // something else changed after it goes into `kept`, as it is now
    void undo(lv_obj_t* o, CssUndo& kept) const;
    // What undo() kept, put on o again (after the new CSS)
    void restore(lv_obj_t* o) const;

private:
    struct Prop {
        lv_style_selector_t sel = 0;
        lv_style_prop_t prop = 0;
        bool had = false;  // a value before CSS
        lv_style_value_t before{}, after{};
    };
    enum Mode : uint8_t { kScrollbar = 1, kScrollDir = 2, kLongMode = 4 };

    P::Array<Prop> props_;
    uint32_t flags_ = 0;  // the flags CSS changed, and how they were before and after it
    uint32_t flagsBefore_ = 0, flagsAfter_ = 0;
    uint8_t modes_ = 0;   // Mode bits: the modes CSS changed
    uint8_t scrollbar_[2] = {}, scrollDir_[2] = {}, longMode_[2] = {};  // before, after
};

}  // namespace UI
