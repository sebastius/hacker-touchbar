#include "mode/mediacenter_mode.hpp"
#include "mode/mode_registry.hpp"
#include "config/config.hpp"
#include "media/mpris_dbus_client.hpp"
#include "audio/audio_source.hpp"
#include "viz/album_art.hpp"
#include "viz/pioneer_hues.hpp"
#include "gfx/theme.hpp"
#include "util/log.hpp"

MediaCenterMode::MediaCenterMode(ModeContext& ctx)
    : transport_(ctx.cfg.mc_transport_width),
      viz_(std::move(ctx.audio), ctx.cfg),
      volume_(ctx.cfg.volume_max_percent, ctx.cfg.mc_volume_width),
      art_fetcher_(ctx.loop),
      flash_seconds_(ctx.cfg.mc_flash_seconds)
{
    mode_btn_.setCallback([this] { fireHome(); });
    mode_btn_.setLongPressCallback([this] { fireCycle(); });
    preset_btn_.setCallback([this] { viz_.cyclePreset(+1); });

    // Fetched artwork lands here (main thread): thumbnail into the
    // overlay's art slot, the two dominant hues into the pioneer
    // palette + a full chrome retint. The art hues are remembered in
    // artHues() so the cover tap can toggle back to them.
    art_fetcher_.setCallback([](const ArtAnalysis& a) {
        AlbumArt& art = albumArt();
        art.w = a.w;
        art.h = a.h;
        art.px = a.px;
        ++art.seq;
        artHues().main = a.main_hue;
        artHues().accent = a.accent_hue;
        pioneerHues().main = a.main_hue;
        pioneerHues().accent = a.accent_hue;
        theme::retint(a.main_hue);
        log_info("Art: %dx%d, hues %.0f/%.0f", a.w, a.h, a.main_hue,
                 a.accent_hue);
    });

    if (ctx.mpris_factory)
        mpris_ = ctx.mpris_factory();
    else
        mpris_ = std::make_unique<MprisDBusClient>(ctx.loop);

    if (mpris_) {
        mpris_->init();
        transport_.setClient(mpris_.get());
        viz_.setMpris(mpris_.get());
        transport_.sync(mpris_->state());

        mpris_->setEventCallback([this](MprisEvent ev) {
            transport_.sync(mpris_->state());
            // Cheap on repeats (the fetcher dedupes by URL) — covers
            // players that appear already holding a track, not just
            // mid-session track changes.
            art_fetcher_.fetch(mpris_->state().art_url);
            if (ev == MprisEvent::TrackChanged ||
                ev == MprisEvent::PlaybackStarted)
                fireTrackChange();  // ModeManager: jump home + flash
        });

        // Daemon (re)started mid-track: fetch whatever art is current.
        art_fetcher_.fetch(mpris_->state().art_url);
    }

    // Modes initialize their own widgets; ModeManager only registers
    // their timers and swaps them in and out of the layout.
    transport_.init();
    viz_.init();
    volume_.init();
}

std::string MediaCenterMode::handleCommand(const std::string& cmd) {
    if (cmd == "preset")
        return "ok " + viz_.presetName();
    if (cmd == "preset next") {
        viz_.cyclePreset(+1);
        return "ok " + viz_.presetName();
    }
    if (cmd == "preset prev") {
        viz_.cyclePreset(-1);
        return "ok " + viz_.presetName();
    }
    if (cmd.rfind("preset ", 0) == 0) {
        std::string name = cmd.substr(7);
        if (viz_.setPreset(name))
            return "ok " + viz_.presetName();
        return "err unknown preset '" + name + "'";
    }
    return "";
}

REGISTER_MODE("mediacenter", MediaCenterMode);
