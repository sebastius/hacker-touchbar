#pragma once
#include <string>
#include <vector>

// Daemon configuration, loaded from ~/.config/touchbar-fx/config.toml.
// Every key has a sane default; a missing or partial file is fine.
struct Config {
    // [modes]
    std::vector<std::string> mode_order = {"mediacenter", "sliders"};

    // [ui]
    std::string ui_theme = "default";   // widget palette ("default" | "oel")

    // [mediacenter] — home mode layout (transport | visualizer | volume)
    int mc_transport_width = 200;   // left transport cluster, pixels
    int mc_volume_width = 300;      // right volume slider, pixels
    int mc_flash_seconds = 2;       // track-text flash after track change

    // [visualizer]
    std::string viz_preset = "waves";  // initial preset (see src/viz/presets/)
    int viz_rotate_seconds = 30;       // auto-rotate presets; 0 disables
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
};
