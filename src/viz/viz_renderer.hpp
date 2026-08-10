#pragma once
#include "viz/preset.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <fftw3.h>

class Framebuffer;
class VizPreset;
struct Config;

// MilkDrop-style CPU render core for the touch bar form factor:
// FFT spectrum analysis + beat detection + a feedback-trail pass
// (each frame the previous one is zoomed/drifted and decayed), then
// the active VizPreset draws on top. Draws into the shared
// Framebuffer so every output backend shows identical pixels.
class VizRenderer {
public:
    VizRenderer(int width, int height, const Config& cfg,
                const std::string& preset_name);
    ~VizRenderer();

    // Feed mono samples in [-1, 1]; call once per frame with the
    // samples captured since the previous frame.
    void feedSamples(const float* samples, size_t count, int sample_rate);

    // Advance feedback state and draw the current frame.
    void render(Framebuffer& fb);

    // Preset switching. setPreset returns false on an unknown name
    // (current preset kept). cyclePreset steps through presetNames().
    bool setPreset(const std::string& name);
    void cyclePreset(int dir);
    const std::string& presetName() const { return preset_name_; }

    // Forward a tap on the viz zone to the active preset.
    void onTap(int x, int y);

    // Forward a long press on the viz zone to the active preset.
    void onLongPress(int x, int y);

    // ---- state exposed to presets ----
    int barCount() const { return num_bars_; }
    const std::vector<float>& bars() const { return smoothed_; }  // 0..1 per bar
    const std::vector<float>& wave() const { return window_; }    // raw mono
    size_t waveFill() const { return window_fill_; }
    float pulse() const { return pulse_; }   // 1.0 on beat, decays
    float hue() const { return hue_; }       // degrees
    bool silent() const { return peak_ < 1e-4f; }

    // Test/diagnostic hook forwarded to the active preset.
    int airborneCount() const {
        return preset_ ? preset_->airborneCount() : 0;
    }

private:
    void analyze();
    void feedbackPass(Framebuffer& fb);

    int w_, h_;

    // config
    int num_bars_;
    float smoothing_;
    float hue_;
    float hue_speed_;    // degrees per frame
    float trail_decay_;
    float trail_zoom_;
    int trail_drift_;

    // active preset
    std::unique_ptr<VizPreset> preset_;
    std::string preset_name_;

    // analysis
    static constexpr int FFT_SIZE = 2048;
    int sample_rate_ = 48000;
    std::vector<float> window_;       // last FFT_SIZE mono samples
    size_t window_fill_ = 0;
    double* fft_in_ = nullptr;
    fftw_complex* fft_out_ = nullptr;
    fftw_plan fft_plan_ = nullptr;
    std::vector<float> mags_;         // this frame, normalized 0..1
    std::vector<float> smoothed_;
    float peak_ = 0.0f;               // auto-gain peak tracker

    // beat detection
    float energy_avg_ = 0.0f;
    float pulse_ = 0.0f;              // 1.0 on beat, decays per frame
    int frames_since_beat_ = 999;

    // feedback
    std::vector<uint32_t> prev_;      // previous frame's pixels
};
