#pragma once
#include "widget/widget.hpp"
#include <functional>

// Small preset-cycle button ("✦" sparkle), shown in the media center
// immediately right of the mode button. Tap = next visualizer preset
// (same as `preset next` IPC; no swipe gesture exists — this
// digitizer never emits Y events).
class PresetButtonWidget : public Widget {
public:
    static constexpr int WIDTH = 50;

    PresetButtonWidget() {
        name_ = "preset-button";
        requested_width_ = WIDTH;
    }

    void update() override {}
    void render(Framebuffer& fb) override;
    void onTap(int x, int y) override;

    void setCallback(std::function<void()> cb) { on_tap_ = std::move(cb); }

private:
    std::function<void()> on_tap_;
};
