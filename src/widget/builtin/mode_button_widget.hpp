#pragma once
#include "widget/widget.hpp"
#include <functional>

// Small mode button pinned at the very left of the bar in every mode.
// Tap = return to the home mode (no-op when already home);
// LONG-PRESS (>= 600 ms) = cycle to the next mode — this is the only
// way into secondary modes by touch, so they can't be entered
// accidentally.
class ModeButtonWidget : public Widget {
public:
    static constexpr int WIDTH = 60;

    ModeButtonWidget() {
        name_ = "mode-button";
        requested_width_ = WIDTH;
    }

    void update() override {}
    void render(Framebuffer& fb) override;
    void onTap(int x, int y) override;
    void onLongPress(int x, int y) override;

    void setCallback(std::function<void()> cb) { on_tap_ = std::move(cb); }
    void setLongPressCallback(std::function<void()> cb) {
        on_long_press_ = std::move(cb);
    }

private:
    std::function<void()> on_tap_;
    std::function<void()> on_long_press_;
};
