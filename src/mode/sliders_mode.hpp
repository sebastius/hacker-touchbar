#pragma once
#include "mode/mode.hpp"
#include "widget/builtin/brightness_widget.hpp"
#include "widget/builtin/mode_button_widget.hpp"

// Secondary mode: [ mode button | brightness slider ] (volume lives
// in the media center now). Unavailable (empty widget list, skipped
// by ModeManager) when no /sys/class/backlight device exists.
class SlidersMode : public Mode {
public:
    explicit SlidersMode(ModeContext&) {
        mode_btn_.setCallback([this] { fireHome(); });
        mode_btn_.setLongPressCallback([this] { fireCycle(); });
        have_brightness_ = BrightnessWidget::available() && brightness_.init();
    }

    const char* name() const override { return "sliders"; }
    std::vector<Widget*> widgets() override {
        if (!have_brightness_) return {};
        return {&mode_btn_, &brightness_};
    }

private:
    ModeButtonWidget mode_btn_;
    BrightnessWidget brightness_;
    bool have_brightness_ = false;
};
