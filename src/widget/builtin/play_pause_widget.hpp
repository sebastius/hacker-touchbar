#pragma once
#include "widget/widget.hpp"
#include "media/mpris_client.hpp"

// One big play/pause key. Acts on touch-down, so it feels like a key.
class PlayPauseWidget : public Widget {
public:
    explicit PlayPauseWidget(int width = 110);

    void update() override {}
    void render(Framebuffer& fb) override;
    void onPress(int x, int y) override;

    void sync(const MprisState& state);
    void setClient(MprisClient* client) { client_ = client; }

private:
    MprisClient* client_ = nullptr;
    bool has_player_ = false;
    bool playing_ = false;
};
