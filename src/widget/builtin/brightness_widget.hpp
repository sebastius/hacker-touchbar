#pragma once
#include "widget/slider_widget.hpp"
#include <string>

// Screen backlight slider via /sys/class/backlight — same thick
// track/knob/full-zone-drag UX as the volume slider (SliderWidget
// base), with a raw sysfs backend. The sun pill on the left is a
// passive icon (taps there do nothing; the track is tapped/dragged).
class BrightnessWidget : public SliderWidget {
public:
    BrightnessWidget();

    bool init() override;
    void update() override;
    void render(Framebuffer& fb) override;
    int timerIntervalMs() const override { return 1000; }

    // True when a backlight device exists (sliders mode includes this
    // widget only then).
    static bool available();

private:
    void applyPercent(int percent) override;  // -> sysfs
    uint32_t fillColor() const override;
    uint32_t knobColor() const override;
    uint32_t textColor() const override;

    void readCurrent();

    std::string dev_path_;   // e.g. /sys/class/backlight/acpi_video0
    bool writable_ = false;
    int max_ = 1;
};
