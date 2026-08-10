#pragma once
#include "mode/mode.hpp"
#include "media/mpris_client.hpp"
#include "media/art_fetcher.hpp"
#include "widget/builtin/mode_button_widget.hpp"
#include "widget/builtin/preset_button_widget.hpp"
#include "widget/builtin/transport_widget.hpp"
#include "widget/builtin/viz_widget.hpp"
#include "widget/builtin/volume_slider_widget.hpp"

// Home mode: [ mode button | preset button | transport | visualizer +
// track overlay + seek strip | volume slider ]. Consumes the daemon's
// AudioSource, owns the MPRIS client (real D-Bus, or a fake under the
// dump/test harness), and handles the "preset ..." IPC commands. On
// each track change it also fetches the player's mpris:artUrl and
// restyles the bar from the artwork (any player — Spotify, YouTube in
// a browser, VLC — publishes art the same way).
class MediaCenterMode : public Mode {
public:
    explicit MediaCenterMode(ModeContext& ctx);

    const char* name() const override { return "mediacenter"; }
    std::vector<Widget*> widgets() override {
        return {&mode_btn_, &preset_btn_, &transport_, &viz_, &volume_};
    }

    std::string handleCommand(const std::string& cmd) override;
    void flashTrack() override { viz_.flashTrack(flash_seconds_); }

private:
    ModeButtonWidget mode_btn_;
    PresetButtonWidget preset_btn_;
    TransportWidget transport_;
    VizWidget viz_;
    VolumeSliderWidget volume_;
    std::unique_ptr<MprisClient> mpris_;
    ArtFetcher art_fetcher_;
    int flash_seconds_;
};
