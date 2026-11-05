#pragma once
#include <string>
#include <vector>

// Daemon configuration, loaded from ~/.config/touchbar-fx/config.toml.
// Every key has a sane default; a missing or partial file is fine.
// One button on the "controls" row. show picks what it displays:
// "label" (the default), "clock", "battery", "theme" or "bar"; the last
// two also act on touch. keys are Linux key names without KEY_.
struct ControlKey {
    std::string show = "label";
    std::string label;
    std::vector<std::string> keys;
    int width = 2;
};

struct Config {
    // [modes]
    std::vector<std::string> mode_order = {"mediacenter", "sliders"};

    // [ui]
    std::string ui_theme = "default";   // "default" | "oel" | "green" | "amber"

    // [mediacenter] — home mode layout (transport | visualizer | volume)
    int mc_transport_width = 200;   // left transport cluster, pixels
    int mc_volume_width = 300;      // right volume slider, pixels
    int mc_flash_seconds = 2;       // track-text flash after track change

    // [visualizer]
    std::string viz_preset = "waves";  // initial preset (see src/viz/presets/)
    int viz_rotate_seconds = 30;       // auto-rotate presets; 0 disables
    // Presets a long press steps through in the deck mode.
    std::vector<std::string> viz_cycle = {"matrix", "life", "stars"};

    // The row a quick Fn tap shows; [[controls]] replaces it entirely.
    std::vector<ControlKey> controls = {
        {"label", "BRI-",   {"BRIGHTNESSDOWN"}, 2},
        {"label", "BRI+",   {"BRIGHTNESSUP"}, 2},
        {"label", "<<",     {"PREVIOUSSONG"}, 2},
        {"label", "PLAY",   {"PLAYPAUSE"}, 2},
        {"label", ">>",     {"NEXTSONG"}, 2},
        {"label", "MUTE",   {"MUTE"}, 2},
        {"label", "VOL-",   {"VOLUMEDOWN"}, 2},
        {"label", "VOL+",   {"VOLUMEUP"}, 2},
        {"label", ">_TERM", {"LEFTCTRL", "LEFTALT", "T"}, 3},
        {"bar",     "", {}, 2},
        {"theme",   "", {}, 3},
        {"clock",   "", {}, 3},
        {"battery", "", {}, 2},
    };
    int viz_bars = 96;
    float viz_smoothing = 0.5f;     // 0..1, higher = snappier bars
    float viz_base_hue = 140.0f;    // degrees (140 = neon green)
    float viz_hue_speed = 8.0f;     // hue cycle, degrees per second
    float viz_trail_decay = 0.93f;  // per-frame trail brightness retention
    float viz_trail_zoom = 1.02f;   // per-frame horizontal feedback zoom
    int viz_trail_drift = 2;        // per-frame horizontal drift, pixels

    // [volume]
    int volume_max_percent = 100;   // slider drag range 0..max_percent

    // [dump] — headless dump backend defaults
    int dump_width = 2170;
    int dump_height = 60;

    // ~/.config/touchbar-fx/config.toml
    static std::string defaultPath();

    // Load from path; returns defaults when the file does not exist.
    // Parse errors are logged and fall back to defaults per-key.
    static Config load(const std::string& path);

    // Theme picked on the bar; remembered beside the default config
    // and preferred over [ui] theme. Empty when nothing was picked.
    static std::string savedTheme();
    static void saveTheme(const std::string& name);

    // Same for the BAR button's brightness; -1 when nothing was picked.
    static int savedBarLevel();
    static void saveBarLevel(int level);
};
