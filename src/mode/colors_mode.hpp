#pragma once
#include "mode/mode.hpp"
#include "viz/pioneer_hues.hpp"
#include "widget/builtin/hue_slider_widget.hpp"
#include "widget/builtin/mode_button_widget.hpp"

// Color picker for the pioneer preset: [ mode button | main hue |
// accent hue ]. The sliders write manualHues() — the user's own
// picks, which album-art sync never overwrites — and copy them to the
// live pioneerHues() so the bar follows immediately. Restoring after
// an art recolor is a tap on the album cover back home.
class ColorsMode : public Mode {
public:
    explicit ColorsMode(ModeContext&) {
        mode_btn_.setCallback([this] { fireHome(); });
        mode_btn_.setLongPressCallback([this] { fireCycle(); });
        main_.init();
        accent_.init();
    }

    const char* name() const override { return "colors"; }
    std::vector<Widget*> widgets() override {
        return {&mode_btn_, &main_, &accent_};
    }

private:
    ModeButtonWidget mode_btn_;
    HueSliderWidget main_{"main hue", &manualHues().main};
    HueSliderWidget accent_{"accent hue", &manualHues().accent};
};
