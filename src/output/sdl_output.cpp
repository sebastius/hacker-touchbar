// Compiled only with -DTOUCHBAR_FX_SCREEN=ON (requires SDL2).
#include "output/sdl_output.hpp"
#include "gfx/framebuffer.hpp"
#include "util/log.hpp"
#include <SDL.h>

SdlOutput::SdlOutput(int scale) : scale_(scale) {}

SdlOutput::~SdlOutput() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool SdlOutput::init(int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        log_error("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow("touchbar-fx",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width, height * scale_, SDL_WINDOW_SHOWN);
    if (!window_) {
        log_error("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        log_error("SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }
    // Framebuffer pixels are 0xAARRGGBB
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture_) {
        log_error("SDL_CreateTexture failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void SdlOutput::pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) quit_ = true;
        else if (ev.type == SDL_KEYDOWN &&
                 (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_q))
            quit_ = true;
    }
}

void SdlOutput::present(const Framebuffer& fb) {
    pumpEvents();
    SDL_UpdateTexture(texture_, nullptr, fb.data(), fb.width() * sizeof(uint32_t));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}
