#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AudioSource;
class EventLoop;
class MprisClient;
class Widget;
struct Config;

// Shared dependencies handed to mode factories by the daemon (or by
// the dump/test harness — the mpris_factory is what lets headless
// runs substitute a fake player).
struct ModeContext {
    EventLoop& loop;
    const Config& cfg;
    std::unique_ptr<AudioSource>& audio;  // consumed by the media center
    // Default (nullptr) means: create the real D-Bus client.
    std::function<std::unique_ptr<MprisClient>()> mpris_factory;
    // Real key events from the Fn layers; unset in headless runs.
    std::function<void(int code, bool down)> key_sink = nullptr;
    // Switch between the terminal themes. Filled in by ModeManager.
    std::function<void()> toggle_theme = nullptr;
    // Touch Bar backlight for the BAR button; unset in headless runs.
    std::function<void()> cycle_bar = nullptr;
    std::function<int()> bar_level = nullptr;
    // Remember a picked theme; unset in headless runs.
    std::function<void(const std::string&)> on_theme_saved = nullptr;
};

// An exclusive full-bar mode. Modes own their widgets, self-register
// a name -> factory pair (REGISTER_MODE), and are instantiated by
// ModeManager according to the TOML [modes] order.
class Mode {
public:
    virtual ~Mode() = default;

    virtual const char* name() const = 0;

    // Widgets shown in the layout while this mode is active. The mode
    // owns them; ModeManager never deletes them. An empty list means
    // the mode is unavailable (skipped with a warning).
    virtual std::vector<Widget*> widgets() = 0;

    virtual void onEnter() {}
    virtual void onExit() {}

    // Mode-specific IPC commands ("preset next", ...). Return an empty
    // string when the command is not handled here.
    virtual std::string handleCommand(const std::string& cmd) {
        (void)cmd;
        return "";
    }

    // Briefly flash the track overlay (media center only).
    virtual void flashTrack() {}

    // Wired by ModeManager: fire when the track changes or playback
    // (re)starts, so the manager can jump to the home mode.
    void setTrackChangeHook(std::function<void()> cb) {
        track_hook_ = std::move(cb);
    }

    // Wired by ModeManager: the mode button (left edge of the bar)
    // returns to the home mode on tap and cycles to the next mode on
    // long-press.
    void setHomeHook(std::function<void()> cb) {
        home_hook_ = std::move(cb);
    }
    void setCycleHook(std::function<void()> cb) {
        cycle_hook_ = std::move(cb);
    }

protected:
    void fireTrackChange() { if (track_hook_) track_hook_(); }
    void fireHome() { if (home_hook_) home_hook_(); }
    void fireCycle() { if (cycle_hook_) cycle_hook_(); }

    std::function<void()> track_hook_;
    std::function<void()> home_hook_;
    std::function<void()> cycle_hook_;
};
