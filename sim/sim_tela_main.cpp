// sim_tela_main.cpp — TelaOS firmware running in the simulator.
//
// The firmware's own src/main.cpp (Arduino setup()/loop()) runs unmodified. The simulator
// supplies the board (SimDevice), the clock (millis/delay), the flash (data/ → LittleFS),
// and eyes and fingers (virtual panel, SDL window, robot).
//
//   telaos_sim                                               window, mouse = finger
//   telaos_sim --headless --script sim/tests/tela_hello.sim --out shots
//   telaos_sim --headless --extra-data sim/tests/fixtures ...  fixture apps on top of data/
#include "sim_panel.h"
#include "sim_robot.h"
#include "sim_tela.h"
#include "sim_window.h"

#include "Arduino.h"
#include "core/core.h"

#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

void setup();  // TelaOS src/main.cpp
void loop();
extern Core g_core;

namespace {

struct Options {
    bool headless = false;
    int zoom = 1;
    std::string out_dir = "shots";
    std::string data_dir = TELAOS_DATA_DIR;
    std::vector<std::string> extra_data;  // test fixtures flashed on top of data/
    std::vector<std::string> scripts;
    std::vector<std::string> runs;
};

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* v = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (a == "--headless") {
            o.headless = true;
            continue;
        }
        if (!v) return false;
        ++i;
        if (a == "--script") o.scripts.push_back(v);
        else if (a == "--run") o.runs.push_back(v);
        else if (a == "--out") o.out_dir = v;
        else if (a == "--zoom") o.zoom = std::atoi(v);
        else if (a == "--data") o.data_dir = v;
        else if (a == "--extra-data") o.extra_data.push_back(v);
        else return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        std::printf("TelaOS simulator: --headless | --script <file> | --run \"<cmds>\" | --out <dir> | "
                    "--zoom <n> | --data <dir> | --extra-data <dir>\n");
        return 2;
    }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    setenv("TZ", "UTC0", 1);  // like the watch before its first BLE time sync
    tzset();

    Sim::Panel panel;
    Sim::PanelConfig pc;
    pc.width = SCREEN_WIDTH;
    pc.height = SCREEN_HEIGHT;
    panel.init(pc);  // the firmware's display HAL creates the LVGL display on top of it

    Sim::Window window;
    const bool has_window = !opt.headless;
    if (has_window && !window.open(panel, opt.zoom, "TelaOS Sim")) {
        std::fprintf(stderr, "[sim] no window available; use --headless\n");
        return 1;
    }

    const int files = SimTela::flashData(opt.data_dir);
    std::printf("[sim] TelaOS: flashed %d files from %s into LittleFS\n", files, opt.data_dir.c_str());
    for (const auto& d : opt.extra_data) {
        std::printf("[sim] TelaOS: + %d fixture files from %s\n", SimTela::flashData(d), d.c_str());
    }

    bool closed = false;
    if (has_window) {
        const uint32_t wall_start = SDL_GetTicks();
        // wall_start by value: the hook outlives this block
        SimTela::setDelayHook([&window, &closed, wall_start](uint32_t) {
            if (!window.pump()) closed = true;
            window.present();
            const uint32_t wall = SDL_GetTicks() - wall_start;
            if (SimTela::now() > wall) SDL_Delay(SimTela::now() - wall);  // pace to the wall clock
        });
    }

    setup();  // firmware boot: display_init() → SimDevice, launcher

    // Virtual time moves only through the firmware's own loop(): each pass ends with delay(5).
    auto advance = [&](uint32_t ms) -> bool {
        const uint32_t target = SimTela::now() + ms;
        while (SimTela::now() < target && !closed) loop();
        return !closed;
    };

    Sim::RobotHooks hooks;
    hooks.findById = [](const std::string& id) { return g_core.get(id.c_str()); };
    hooks.stateVar = [](const std::string& var, std::string& value) {
        if (!g_core.store().has(var.c_str())) return false;
        value = g_core.store().getAsString(var.c_str()).c_str();
        return true;
    };
    hooks.console = [](const std::string& line) { Serial.appendInput((line + "\n").c_str()); };

    Sim::Robot robot(panel, advance, opt.out_dir, hooks);
    bool ok = true;
    for (const auto& s : opt.scripts) {
        if (ok && !robot.quitRequested() && !closed) ok = robot.runFile(s);
    }
    for (const auto& r : opt.runs) {
        if (ok && !robot.quitRequested() && !closed) ok = robot.runText(r);
    }

    if (has_window && !closed && !robot.quitRequested()) {
        while (advance(100)) {
        }
    } else if (!has_window && opt.scripts.empty() && opt.runs.empty()) {
        advance(2000);
        lv_refr_now(nullptr);
        panel.savePng(opt.out_dir + "/screen.png");
    }

    std::printf("[sim] virtual time %u ms, %u frames, %u flushes\n", SimTela::now(), panel.frames(),
                panel.flushes());
    std::printf("[sim] robot: %d passed, %d failed\n", robot.passed(), robot.failed());
    if (has_window) window.close();
    return (ok && robot.failed() == 0) ? 0 : 1;
}
