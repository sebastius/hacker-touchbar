#pragma once
#include "widget/widget.hpp"
#include "media/mpris_client.hpp"

// Left zone of the media center: prev / play-pause / next pill
// cluster. Always visible and tappable; glyphs dim and taps are
// ignored when no MPRIS player exists.
class TransportWidget : public Widget {
public:
    explicit TransportWidget(int width = 200);

    void update() override {}
    void render(Framebuffer& fb) override;
    void onTap(int x, int y) override;

    // Called by the media center mode when MPRIS state changes.
    void sync(const MprisState& state);

    // The client is not owned; outlives the widget in practice (mode).
    void setClient(MprisClient* client) { client_ = client; }

private:
    static constexpr int PAD = 10;  // side padding
    static constexpr int GAP = 10;  // gap between pills

    MprisClient* client_ = nullptr;
    bool has_player_ = false;
    bool playing_ = false;
};
