#pragma once
#include "widget/widget.hpp"
#include <cstdint>

class ClockWidget : public Widget {
public:
    ClockWidget();

    bool init() override;
    void update() override;
    void render(Framebuffer& fb) override;
    int timerIntervalMs() const override { return 1000; }

private:
    uint32_t color_;
    char time_str_[16];
};
