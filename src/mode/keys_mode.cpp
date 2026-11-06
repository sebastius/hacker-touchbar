#include "mode/keys_mode.hpp"
#include "mode/mode_registry.hpp"
#include "config/config.hpp"
#include "media/mpris_dbus_client.hpp"
#include "util/log.hpp"
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <algorithm>
#include <cctype>

using Key = KeyRowWidget::Key;
using Show = KeyRowWidget::Show;

static std::unique_ptr<Mode> make_fkeys(ModeContext& ctx) {
    std::vector<Key> keys;
    const int codes[12] = {KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
                           KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12};
    for (int i = 0; i < 12; ++i)
        keys.push_back({Show::Label, "F" + std::to_string(i + 1), {codes[i]}, 1, nullptr});
    return std::make_unique<KeysMode>("fkeys", std::move(keys), ctx.key_sink,
                                      false, ctx.cfg);
}

// "VOLUMEUP" or "volumeup" -> KEY_VOLUMEUP; -1 when there is no such key.
static int key_code(std::string name) {
    for (char& c : name) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return libevdev_event_code_from_name(EV_KEY, ("KEY_" + name).c_str());
}

// Bad entries are logged and skipped: a typo must not cost the whole row.
static bool build_key(const ControlKey& c, ModeContext& ctx, Key& key) {
    key = {Show::Label, c.label, {}, 1, nullptr};
    key.min_px = c.width;
    key.blink = c.blink;
    if (c.show == "gap") {
        key.show = Show::Gap;
        if (key.min_px <= 0) key.min_px = 40;
    } else if (c.show == "clock") {
        key.show = Show::Clock;
        key.fit_text = "00:00";
    } else if (c.show == "battery") {
        key.show = Show::Battery;
        key.fit_text = "100%";
    } else if (c.show == "theme") {
        key.show = Show::ThemeDot;
        key.action = [&ctx] { if (ctx.toggle_theme) ctx.toggle_theme(); };
    } else if (c.show == "playpause") {
        key.codes = {KEY_PLAYPAUSE};
        key.fit_text = "PAUSE";
        key.text = [&ctx] {
            return std::string(ctx.is_playing && ctx.is_playing() ? "PAUSE" : "PLAY");
        };
    } else if (c.show == "bar") {
        key.fit_text = "_";
        key.action = [&ctx] { if (ctx.cycle_bar) ctx.cycle_bar(); };
        key.text = [&ctx] {  // dim is a thin line, full the thick DOS bar
            const int level = ctx.bar_level ? ctx.bar_level() : -1;
            return std::string(level >= 2 ? "\x16" : level == 1 ? "_" : "-");
        };
    } else if (c.show != "label") {
        log_warn("[[controls]]: unknown show '%s' - skipped", c.show.c_str());
        return false;
    } else {
        for (const std::string& name : c.keys) {
            int code = key_code(name);
            if (code < 0) {
                log_warn("[[controls]] '%s': unknown key '%s' - skipped",
                         c.label.c_str(), name.c_str());
                return false;
            }
            key.codes.push_back(code);
        }
        if (key.codes.empty()) {
            log_warn("[[controls]] '%s': no keys - skipped", c.label.c_str());
            return false;
        }
    }
    return true;
}

static std::unique_ptr<Mode> make_controls(ModeContext& ctx) {
    std::vector<Key> keys;
    for (const ControlKey& c : ctx.cfg.controls) {
        Key key;
        if (build_key(c, ctx, key)) keys.push_back(std::move(key));
    }
    auto mode = std::make_unique<KeysMode>("controls", std::move(keys), ctx.key_sink,
                                           true, ctx.cfg);

    // As the home mode there is no deck to report the player's state.
    const auto& order = ctx.cfg.mode_order;
    if (!ctx.is_playing && std::find(order.begin(), order.end(), "controls") != order.end()) {
        std::unique_ptr<MprisClient> player = ctx.mpris_factory
            ? ctx.mpris_factory()
            : std::make_unique<MprisDBusClient>(ctx.loop);
        player->init();
        MprisClient* client = player.get();
        ctx.is_playing = [client] { return client->state().playing; };
        mode->setPlayer(std::move(player));
    }
    return mode;
}

static const bool fkeys_registered = registerModeFactory("fkeys", make_fkeys);
static const bool controls_registered =
    registerModeFactory("controls", make_controls);
