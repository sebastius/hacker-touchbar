#include "mode/deck_mode.hpp"
#include "mode/mode_registry.hpp"
#include "audio/audio_source.hpp"
#include "config/config.hpp"
#include "media/mpris_dbus_client.hpp"

DeckMode::DeckMode(ModeContext& ctx)
    : volume_(ctx.cfg.volume_max_percent, ctx.cfg.mc_volume_width),
      viz_(std::move(ctx.audio), ctx.cfg)
{
    viz_.setCycleOnLongPress(true);

    if (ctx.mpris_factory)
        mpris_ = ctx.mpris_factory();
    else
        mpris_ = std::make_unique<MprisDBusClient>(ctx.loop);

    if (mpris_) {
        mpris_->init();
        play_.setClient(mpris_.get());
        play_.sync(mpris_->state());
        mpris_->setEventCallback([this](MprisEvent) {
            play_.sync(mpris_->state());
        });
    }

    viz_.init();
    volume_.init();
}

std::string DeckMode::handleCommand(const std::string& cmd) {
    if (cmd == "preset") return "ok " + viz_.presetName();
    if (cmd == "preset next") {
        viz_.nextInCycle();
        return "ok " + viz_.presetName();
    }
    if (cmd.rfind("preset ", 0) == 0) {
        const std::string name = cmd.substr(7);
        if (viz_.setPreset(name)) return "ok " + viz_.presetName();
        return "err unknown preset '" + name + "'";
    }
    return "";
}

REGISTER_MODE("deck", DeckMode);
