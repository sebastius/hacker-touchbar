#include "widget/builtin/hue_slider_widget.hpp"
#include "viz/pioneer_hues.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include "gfx/theme.hpp"

HueSliderWidget::HueSliderWidget(const char* name, float* target)
    : SliderWidget(396, 0), target_(target) {  // 0..360 hues + white zone
    name_ = name;
    // requested_width_ = 0: flexible — the colors mode stacks two of
    // these and lets them split the bar.
}

bool HueSliderWidget::init() {
    percent_ = static_cast<int>(*target_ + 0.5f);
    return true;
}

void HueSliderWidget::applyPercent(int percent) {
    // Past 360 the slider is in the white zone — stored as-is, every
    // consumer treats >360 as zero saturation (see pioneer_hues.hpp).
    *target_ = static_cast<float>(percent);
    // Sliders edit the user's own picks; copy them to the live scheme
    // (overriding any album-art colors) and retint the whole OEL
    // chrome — buttons, transport, sliders, frames — on main-hue
    // drags.
    pioneerHues().main = manualHues().main;
    pioneerHues().accent = manualHues().accent;
    if (target_ == &manualHues().main)
        theme::retint(*target_);
}

uint32_t HueSliderWidget::fillColor() const {
    return hue_rgb(*target_, 0.90f, 0.75f);
}

uint32_t HueSliderWidget::knobColor() const {
    return hue_rgb(*target_, 0.40f, 1.0f);
}

uint32_t HueSliderWidget::textColor() const {
    return theme::text_dim;
}

void HueSliderWidget::render(Framebuffer& fb) {
    fb.clear(0xFF000000);
    // Swatch disc in the current hue, with a dark rim so very dim
    // hues still read as a disc.
    fb.fillCircle(ADORN_W / 2, fb.height() / 2, 12, fillColor());
    fb.drawRect(ADORN_W / 2 - 12, fb.height() / 2 - 12, 24, 24,
                theme::btn_border);
    drawSlider(fb);
}
