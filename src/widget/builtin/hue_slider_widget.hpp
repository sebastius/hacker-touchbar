#pragma once
#include "widget/slider_widget.hpp"

// Hue slider (0..360 degrees, plus a white zone at the top of the
// range) bound to one of the user's manual colors (manualHues(), see
// src/viz/pioneer_hues.hpp) — edits copy straight into the live
// pioneerHues() so the preset picks the change up on its next frame.
// The main-hue slider also retints the OEL theme's chrome, so the
// whole bar follows. Same thick
// track/knob/full-zone drag UX as the volume and brightness sliders;
// the left adornment is a swatch disc in the current hue.
class HueSliderWidget : public SliderWidget {
public:
    // `target` must outlive the widget (manualHues() is static).
    HueSliderWidget(const char* name, float* target);

    bool init() override;
    void update() override {}
    void render(Framebuffer& fb) override;

private:
    void applyPercent(int percent) override;  // -> *target_
    uint32_t fillColor() const override;
    uint32_t knobColor() const override;
    uint32_t textColor() const override;

    float* target_;
};
