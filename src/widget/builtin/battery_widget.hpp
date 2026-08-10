#pragma once
#include "widget/widget.hpp"
#include <cstdint>

class BatteryWidget : public Widget {
public:
    BatteryWidget();

    bool init() override;
    void update() override;
    void render(Framebuffer& fb) override;
    int timerIntervalMs() const override { return 30000; } // 30s

private:
    int readCapacity();
    bool readCharging();

    int percent_;
    bool charging_;
    uint32_t color_good_;
    uint32_t color_warn_;
    uint32_t color_crit_;
};
