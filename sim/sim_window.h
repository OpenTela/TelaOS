// sim_window.h — SDL2 window for humans: shows the panel GRAM, mouse = finger.
#pragma once

#include "sim_panel.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace Sim {

class Window {
public:
    bool open(Panel& panel, int zoom, const char* title);
    bool pump();     // handles SDL events; returns false when the window is closed
    void present();  // shows the GRAM if it changed
    void close();

private:
    Panel* panel_ = nullptr;
    SDL_Window* win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture* tex_ = nullptr;
    int zoom_ = 1;
    bool mouse_down_ = false;
    bool force_present_ = true;
};

}  // namespace Sim
