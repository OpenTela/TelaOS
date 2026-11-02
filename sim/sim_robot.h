// sim_robot.h — scripted "robot finger": taps, drags, screenshots and checks.
//
// Script: one command per line (or ';'-separated), '#' starts a comment.
// Targets are one token:
//   @id            widget by id (TelaOS element id; lv_obj_set_name on LVGL 9.3+)
//   @id:fx,fy      point inside the widget box, fractions 0..1 (e.g. @slider:0.9,0.5)
//   "text"         visible label / span paragraph / button-matrix key / dropdown or roller line
//   x,y            raw panel coordinates
// Commands:
//   wait <ms> | tap <T> | longpress <T> [ms] | drag <T1> <T2> [ms]
//   press <T> | move <T> | release | scroll @id
//   text @id <value>         set textarea/label text (same as the BLE `text` command)
//   expect @id <value>       check widget value (a container: the text it shows); ranges: 20..30
//   expect @id contains <v>  substring of the value
//   expect <@id|"text"> :visible | :hidden   on screen, or hidden / scrolled out of view
//   expect $var <value>      check app state variable (TelaOS)
//   > <console line>         type a line into the firmware serial console (TelaOS)
//   shot <name>              save <out>/<name>.png
//   tree <name>              save <out>/<name>.json: widget tree with boxes and values
//   log <text> | quit
#pragma once

#include "sim_panel.h"

#include <functional>
#include <string>
#include <vector>

namespace Sim {

// What the firmware under test exposes to the robot. Empty hooks = feature unavailable.
struct RobotHooks {
    std::function<lv_obj_t*(const std::string& id)> findById;
    std::function<bool(const std::string& var, std::string& value)> stateVar;
    std::function<void(const std::string& line)> console;
};

class Robot {
public:
    // Must run the LVGL loop for `ms` of virtual time; returns false to stop.
    using Advance = std::function<bool(uint32_t ms)>;

    Robot(Panel& panel, Advance advance, std::string out_dir, RobotHooks hooks = {});

    bool runFile(const std::string& path);
    bool runText(const std::string& text, const std::string& origin = "--run");

    int passed() const { return passed_; }
    int failed() const { return failed_; }
    bool quitRequested() const { return quit_; }

private:
    struct Point {
        int x = 0;
        int y = 0;
    };

    bool exec(const std::vector<std::string>& a, const std::string& where);
    bool resolve(const std::string& target, Point& p, std::string& err);
    bool tap(const Point& p, uint32_t hold_ms);
    bool drag(const Point& from, const Point& to, uint32_t ms);
    bool saveTree(const std::string& path);

    lv_obj_t* findById(const std::string& id) const;

    Panel& panel_;
    Advance advance_;
    std::string out_dir_;
    RobotHooks hooks_;
    int passed_ = 0;
    int failed_ = 0;
    bool quit_ = false;
};

}  // namespace Sim
