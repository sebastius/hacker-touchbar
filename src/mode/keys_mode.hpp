#pragma once
#include "config/config.hpp"
#include "media/mpris_client.hpp"
#include "mode/mode.hpp"
#include "widget/builtin/key_row_widget.hpp"

// Full-bar rows of real keys. "fkeys" shows while Fn is held. "controls"
// is the everyday row: the home mode, or a layer on top of another mode
// that a quick Fn tap keeps on screen.
class KeysMode : public Mode {
public:
    KeysMode(const char* name, std::vector<KeyRowWidget::Key> keys,
             KeyRowWidget::Sink sink, bool fit, const Config& cfg)
        : name_(name), row_(std::move(keys), std::move(sink), fit, cfg.key_gap) {
        if (!cfg.key_background.empty())
            row_.setBackground(cfg, cfg.key_background);
    }

    const char* name() const override { return name_; }
    std::vector<Widget*> widgets() override { return {&row_}; }

    // Keeps the player client alive for the PLAY/PAUSE key.
    void setPlayer(std::unique_ptr<MprisClient> player) {
        player_ = std::move(player);
    }

private:
    const char* name_;
    KeyRowWidget row_;
    std::unique_ptr<MprisClient> player_;
};
