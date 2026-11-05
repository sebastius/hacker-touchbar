#include "widget/builtin/play_pause_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"

PlayPauseWidget::PlayPauseWidget(int width) {
    name_ = "playpause";
    requested_width_ = width;
}

void PlayPauseWidget::sync(const MprisState& state) {
    if (state.has_player != has_player_ || state.playing != playing_) {
        has_player_ = state.has_player;
        playing_ = state.playing;
        markDirty();
    }
}

void PlayPauseWidget::onPress(int, int) {
    if (client_ && has_player_) client_->playPause();
}

void PlayPauseWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);
    const int w = fb.width(), h = fb.height();
    theme::drawButton(fb, 2, 1, w - 4, h - 2);

    const uint32_t c = has_player_ ? theme::text : theme::text_muted;
    const int cx = w / 2, cy = h / 2;
    if (playing_) {
        fb.fillRect(cx - 15, cy - 18, 11, 36, c);
        fb.fillRect(cx + 4, cy - 18, 11, 36, c);
    } else {
        for (int i = 0; i < 18; ++i)  // stepped triangle, two px per step
            fb.fillRect(cx - 14 + 2 * i, cy - 18 + i, 2, 36 - 2 * i, c);
    }
}
