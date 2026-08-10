#pragma once
#include "output/output.hpp"

// NOTE: this backend is only compiled when CMake is configured with
// -DTOUCHBAR_FX_SCREEN=ON (requires SDL2, `sudo pacman -S sdl2`).

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// On-screen window backend: mirrors the touch bar framebuffer in an
// SDL2 window (scaled up vertically so the 60px strip is visible).
class SdlOutput : public Output {
public:
    explicit SdlOutput(int scale = 4);
    ~SdlOutput();

    bool init(int width, int height);
    void present(const Framebuffer& fb) override;

    // True after the window is closed or Esc/Q is pressed.
    bool quitRequested() const { return quit_; }

private:
    void pumpEvents();

    int scale_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool quit_ = false;
};
