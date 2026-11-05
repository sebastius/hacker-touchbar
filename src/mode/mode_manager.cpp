#include "mode/mode_manager.hpp"
#include "mode/mode_registry.hpp"
#include "config/config.hpp"
#include "engine/event_loop.hpp"
#include "engine/scheduler.hpp"
#include "widget/layout_engine.hpp"
#include "widget/widget.hpp"
#include "viz/pioneer_hues.hpp"
#include "viz/album_art.hpp"
#include "gfx/theme.hpp"
#include "util/log.hpp"
#include "util/timer_fd.hpp"
#include <sys/epoll.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

ModeManager::ModeManager(EventLoop& loop, LayoutEngine& layout,
                         Scheduler& sched, const Config& cfg,
                         ModeContext& ctx)
    : loop_(loop), layout_(layout), sched_(sched), order_(cfg.mode_order)
{
    if (order_.empty()) order_.push_back("mediacenter");

    on_theme_saved_ = ctx.on_theme_saved;
    ctx.toggle_theme = [this] {
        setTheme(theme::current_name == "amber" ? "green" : "amber");
    };

    // Instantiate configured modes from the registry.
    for (const std::string& name : order_) {
        std::unique_ptr<Mode> mode = createMode(name, ctx);
        if (!mode) {
            log_warn("Unknown mode '%s' in [modes] order — skipping",
                     name.c_str());
            continue;
        }
        if (mode->widgets().empty()) {
            log_warn("Mode '%s' unavailable — skipping", name.c_str());
            continue;
        }
        mode->setTrackChangeHook([this] { notifyTrackChanged(); });
        mode->setHomeHook([this] {
            // Mode button tap: return home (first configured mode).
            for (const auto& name : order_) {
                if (modes_.count(name)) { setMode(name); return; }
            }
        });
        mode->setCycleHook([this] { cycle(+1); });
        modes_[name] = std::move(mode);
    }

    // Fn layers: reachable by key only, so never added to order_.
    for (const char* name : {"fkeys", "controls"}) {
        if (std::unique_ptr<Mode> mode = createMode(name, ctx))
            modes_[name] = std::move(mode);
    }

    // Per-widget timers tick regardless of which mode is visible.
    for (auto& [name, mode] : modes_) {
        for (auto* wp : mode->widgets()) {
            int interval = wp->timerIntervalMs();
            if (interval <= 0) continue;
            int tfd = timerfd_create_ms(interval, true);
            if (tfd < 0) continue;
            loop_.addFd(tfd, EPOLLIN, [wp, tfd](uint32_t) {
                timerfd_read(tfd);
                wp->update();
            });
            log_debug("Timer for '%s': %d ms (fd=%d)",
                      wp->name().c_str(), interval, tfd);
        }
    }
}

bool ModeManager::start() {
    if (modes_.empty()) {
        log_error("No usable modes configured");
        return false;
    }
    for (const auto& name : order_) {
        if (modes_.count(name)) return setMode(name);
    }
    return false;
}

bool ModeManager::setMode(const std::string& name) {
    auto it = modes_.find(name);
    if (it == modes_.end()) {
        log_warn("Unknown mode '%s'", name.c_str());
        return false;
    }
    if (name != current_) {
        auto cur = modes_.find(current_);
        if (cur != modes_.end()) cur->second->onExit();
        log_info("Mode: %s -> %s", current_.c_str(), name.c_str());
        current_ = name;
        it->second->onEnter();
    }
    layout_.setWidgetsBorrowed(it->second->widgets(), sched_);
    sched_.markDirty();
    sched_.flush();
    return true;
}

void ModeManager::cycle(int dir) {
    if (modes_.empty()) return;
    int idx = 0;
    for (size_t i = 0; i < order_.size(); ++i) {
        if (order_[i] == current_) { idx = static_cast<int>(i); break; }
    }
    int n = static_cast<int>(order_.size());
    for (int step = 0; step < n; ++step) {
        idx = (idx + dir + n) % n;
        if (modes_.count(order_[idx])) {
            setMode(order_[idx]);
            return;
        }
    }
}

void ModeManager::applyLayer() {
    setMode(fn_held_ ? "fkeys" : controls_on_ ? "controls" : base_);
}

void ModeManager::setFnHeld(bool held) {
    if (held && !layerActive()) base_ = current_;
    fn_held_ = held;
    applyLayer();
}

void ModeManager::toggleControls() {
    if (!layerActive()) base_ = current_;
    controls_on_ = !controls_on_;
    applyLayer();
}

bool ModeManager::setTheme(const std::string& name) {
    if (name != "green" && name != "amber") return false;
    theme::apply(Theme::fromName(name), name);
    if (on_theme_saved_) on_theme_saved_(name);
    sched_.markDirty();
    return true;
}

void ModeManager::notifyTrackChanged() {
    if (layerActive()) return;  // never pull keys out from under the user
    // Home mode is the media center: jump there and flash the track.
    for (const auto& name : order_) {
        auto it = modes_.find(name);
        if (it != modes_.end()) {
            setMode(name);
            it->second->flashTrack();
            return;
        }
    }
}

std::string ModeManager::handleCommand(const std::string& cmd) {
    if (cmd == "cycle") {
        cycle(+1);
        return "ok " + current_;
    }
    if (cmd.rfind("mode ", 0) == 0) {
        std::string name = cmd.substr(5);
        if (setMode(name))
            return "ok " + current_;
        return "err unknown mode '" + name + "'";
    }
    if (cmd == "mode")
        return "ok " + current_;

    if (cmd.rfind("theme ", 0) == 0) {
        std::string name = cmd.substr(6);
        return setTheme(name) ? "ok " + name : "err usage: theme green|amber";
    }

    // "hue" reports, "hue <main> [<accent>]" sets the user's manual
    // colors (0-360, past 360 = white), applies them live, and
    // retints the OEL chrome — same as the colors mode sliders.
    // Album-art hues overwrite the live scheme per track but never
    // the manual picks.
    if (cmd == "hue" || cmd.rfind("hue ", 0) == 0) {
        char buf[96];
        if (cmd == "hue") {
            snprintf(buf, sizeof(buf), "ok main=%.0f accent=%.0f",
                     pioneerHues().main, pioneerHues().accent);
            return buf;
        }
        char* end = nullptr;
        const float main_h = strtof(cmd.c_str() + 4, &end);
        if (end == cmd.c_str() + 4 || main_h < 0.0f || main_h > 396.0f)
            return "err usage: hue <main 0-396> [accent 0-396]";
        float accent_h = strtof(end, &end);
        pioneerHues().main = main_h;
        manualHues().main = main_h;
        if (accent_h >= 0.0f && accent_h <= 396.0f) {
            pioneerHues().accent = accent_h;
            manualHues().accent = accent_h;
        }
        theme::retint(main_h);
        snprintf(buf, sizeof(buf), "ok hue %.0f/%.0f", pioneerHues().main,
                 pioneerHues().accent);
        return buf;
    }

    // "art <w> <h> <hex rgb332>" — album art thumbnail pushed by an
    // external client (the in-daemon path is the MPRIS art fetcher).
    // The media center overlay draws it beside the track text.
    if (cmd.rfind("art ", 0) == 0) {
        const char* p = cmd.c_str() + 4;
        char* end = nullptr;
        const long aw = strtol(p, &end, 10);
        if (end == p) return "err art dims";
        p = end;
        const long ah = strtol(p, &end, 10);
        if (end == p) return "err art dims";
        p = end;
        while (*p == ' ') ++p;
        if (aw <= 0 || ah <= 0 || aw > 64 || ah > 64)
            return "err art dims";
        const size_t npix = static_cast<size_t>(aw) * ah;
        if (strlen(p) < npix * 4) return "err art short";
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        AlbumArt& a = albumArt();
        std::vector<uint32_t> px(npix);
        for (size_t i = 0; i < npix; ++i) {
            int v = 0;
            for (int k = 0; k < 4; ++k) {
                const int n = nib(p[i * 4 + k]);
                if (n < 0) return "err art hex";
                v = (v << 4) | n;
            }
            px[i] = rgb565_to_argb(static_cast<uint16_t>(v));
        }
        a.w = static_cast<int>(aw);
        a.h = static_cast<int>(ah);
        a.px = std::move(px);
        ++a.seq;
        char buf[40];
        snprintf(buf, sizeof(buf), "ok art %ldx%ld", aw, ah);
        return buf;
    }

    // Mode-specific commands ("preset next", ...): first mode that
    // answers wins.
    for (const auto& name : order_) {
        auto it = modes_.find(name);
        if (it == modes_.end()) continue;
        std::string reply = it->second->handleCommand(cmd);
        if (!reply.empty()) return reply;
    }
    return "err unknown command '" + cmd + "'";
}
