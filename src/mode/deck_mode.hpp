#pragma once
#include "mode/mode.hpp"
#include "media/mpris_client.hpp"
#include "widget/builtin/play_pause_widget.hpp"
#include "widget/builtin/viz_widget.hpp"
#include "widget/builtin/volume_slider_widget.hpp"

// Minimal home mode: [ play/pause | volume | animation ]. A long press
// on the animation steps through [visualizer] cycle.
class DeckMode : public Mode {
public:
    explicit DeckMode(ModeContext& ctx);

    const char* name() const override { return "deck"; }
    std::vector<Widget*> widgets() override {
        return {&play_, &volume_, &viz_};
    }
    std::string handleCommand(const std::string& cmd) override;

private:
    PlayPauseWidget play_;
    VolumeSliderWidget volume_;
    VizWidget viz_;
    std::unique_ptr<MprisClient> mpris_;
};
