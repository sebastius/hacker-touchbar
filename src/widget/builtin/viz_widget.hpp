#pragma once
#include "widget/widget.hpp"
#include "media/mpris_client.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class AudioSource;
class VizRenderer;
struct Config;

// Center zone of the media center (and the full-bar visualizer in
// dump mode): drives the shared VizRenderer core from an AudioSource.
// When an MprisClient is attached, the current track title/artist is
// overlaid on the visualizer (marquee scroll behind a dimmed
// backdrop) and a 3 px seek strip runs along the bottom edge — any
// horizontal drag on the zone scrubs (the digitizer reports no Y, so
// hit-testing is X-only). Presets auto-rotate every [visualizer]
// rotate_seconds and can be switched manually (✦ button / IPC), which
// pauses the rotation for 2x the interval.
class VizWidget : public Widget {
public:
    VizWidget(std::unique_ptr<AudioSource> source, const Config& cfg);
    ~VizWidget();

    bool init() override;
    void shutdown() override;
    void update() override;
    void render(Framebuffer& fb) override;
    int timerIntervalMs() const override { return 16; }  // ~60 fps

    bool dragCapableAt(int x, int y) const override;
    void onDrag(int x, int y, DragPhase phase) override;
    void onTap(int x, int y) override;        // cover: toggle manual/art hues; else preset
    void onLongPress(int x, int y) override;  // forwarded to the active preset

    // Not owned; the media center mode keeps the client alive.
    void setMpris(MprisClient* client) { mpris_ = client; }

    // Brighten the track overlay for a while (track-change flash).
    void flashTrack(int seconds);

    // Preset control (IPC "preset ..." and auto-rotate).
    bool setPreset(const std::string& name);   // manual: pauses rotation
    void cyclePreset(int dir);                 // manual: pauses rotation
    std::string presetName() const;

    // Deck mode: a long press moves through [visualizer] cycle instead
    // of going to the preset.
    void setCycleOnLongPress(bool on) { cycle_on_long_press_ = on; }
    void nextInCycle();

private:
    void ensureRenderer(int w, int h);
    void drawOverlay(Framebuffer& fb);
    void drawSeekStrip(Framebuffer& fb);
    int64_t nowMs() const;

    std::unique_ptr<AudioSource> source_;
    const Config& cfg_;
    std::unique_ptr<VizRenderer> viz_;  // created on first render (needs fb size)
    std::vector<float> buf_;

    MprisClient* mpris_ = nullptr;
    bool cycle_on_long_press_ = false;

    // preset state (applied to viz_ lazily, before it exists)
    std::string pending_preset_;
    int64_t next_rotate_ms_ = 0;   // steady-clock ms; 0 = rotation off

    // overlay
    int scroll_offset_ = 0;
    int64_t flash_until_ms_ = 0;

    // seek drag throttling (SetPosition floods the bus otherwise)
    int64_t last_seek_ms_ = 0;

    int bar_h_ = 60;  // last rendered height, for the cover-tap rect

    static constexpr int SEEK_STRIP_H = 3;   // drawn strip height
    static constexpr int SEEK_MIN_MS = 80;   // min interval between seeks
};
