#pragma once
#include "widget/widget.hpp"
#include <cstdint>

class SysmonWidget : public Widget {
public:
    SysmonWidget();

    bool init() override;
    void update() override;
    void render(Framebuffer& fb) override;
    int timerIntervalMs() const override { return 2000; } // 2s

private:
    void readCpu();
    void readRam();
    void readTemp();

    int cpu_percent_;
    int ram_percent_;
    int temp_celsius_;

    // For CPU delta calculation
    long prev_total_;
    long prev_idle_;
};
