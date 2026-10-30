// sim_window.cpp — SDL2 presenter for the virtual panel.
#include "sim_window.h"

#include <SDL.h>

#include <cstdio>

namespace Sim {

bool Window::open(Panel& panel, int zoom, const char* title) {
    panel_ = &panel;
    zoom_ = zoom < 1 ? 1 : zoom;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[sim] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // nearest: crisp pixels when zoomed
    win_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            panel.width() * zoom_, panel.height() * zoom_, SDL_WINDOW_SHOWN);
    if (!win_) {
        std::fprintf(stderr, "[sim] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
    if (!ren_) {
        std::fprintf(stderr, "[sim] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                             panel.width(), panel.height());
    if (!tex_) {
        std::fprintf(stderr, "[sim] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_RendererInfo info{};
    SDL_GetRendererInfo(ren_, &info);
    std::printf("[sim] window %dx%d (zoom %d), SDL video '%s', renderer '%s'\n",
                panel.width() * zoom_, panel.height() * zoom_, zoom_, SDL_GetCurrentVideoDriver(),
                info.name);
    return true;
}

bool Window::pump() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                return false;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) return false;
                break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_CLOSE) return false;
                if (ev.window.event == SDL_WINDOWEVENT_EXPOSED) force_present_ = true;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    mouse_down_ = true;
                    panel_->touch(ev.button.x / zoom_, ev.button.y / zoom_, true);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    mouse_down_ = false;
                    panel_->touch(ev.button.x / zoom_, ev.button.y / zoom_, false);
                }
                break;
            case SDL_MOUSEMOTION:
                if (mouse_down_) panel_->touch(ev.motion.x / zoom_, ev.motion.y / zoom_, true);
                break;
            default:
                break;
        }
    }
    return true;
}

void Window::present() {
    if (!panel_->takeDirty() && !force_present_) return;
    force_present_ = false;
    SDL_UpdateTexture(tex_, nullptr, panel_->gram(), panel_->width() * 2);
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    SDL_RenderPresent(ren_);
}

void Window::close() {
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    tex_ = nullptr;
    ren_ = nullptr;
    win_ = nullptr;
    SDL_Quit();
}

}  // namespace Sim
