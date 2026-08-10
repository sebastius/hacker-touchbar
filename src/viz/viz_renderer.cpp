#include "viz/viz_renderer.hpp"
#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "config/config.hpp"
#include "gfx/framebuffer.hpp"
#include "util/log.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

VizRenderer::VizRenderer(int width, int height, const Config& cfg,
                         const std::string& preset_name)
    : w_(width), h_(height),
      num_bars_(cfg.viz_bars),
      smoothing_(cfg.viz_smoothing),
      hue_(cfg.viz_base_hue),
      hue_speed_(cfg.viz_hue_speed / 60.0f),
      trail_decay_(cfg.viz_trail_decay),
      trail_zoom_(cfg.viz_trail_zoom),
      trail_drift_(cfg.viz_trail_drift)
{
    if (trail_zoom_ < 1.0f) trail_zoom_ = 1.0f;
    window_.resize(FFT_SIZE, 0.0f);
    mags_.resize(num_bars_, 0.0f);
    smoothed_.resize(num_bars_, 0.0f);
    prev_.resize(static_cast<size_t>(w_) * h_, 0xFF000000);

    if (!setPreset(preset_name)) {
        auto names = presetNames();
        if (!names.empty()) {
            log_warn("Unknown viz preset '%s' — falling back to '%s'",
                     preset_name.c_str(), names.front().c_str());
            setPreset(names.front());
        } else {
            log_error("No viz presets registered");
        }
    }

    fft_in_ = fftw_alloc_real(FFT_SIZE);
    fft_out_ = fftw_alloc_complex(FFT_SIZE / 2 + 1);
    if (!fft_in_ || !fft_out_) {
        log_error("VizRenderer: failed to allocate FFT buffers");
        return;
    }
    fft_plan_ = fftw_plan_dft_r2c_1d(FFT_SIZE, fft_in_, fft_out_, FFTW_MEASURE);
    if (!fft_plan_)
        log_error("VizRenderer: failed to create FFT plan");
}

VizRenderer::~VizRenderer() {
    if (fft_plan_) fftw_destroy_plan(fft_plan_);
    if (fft_in_) fftw_free(fft_in_);
    if (fft_out_) fftw_free(fft_out_);
}

bool VizRenderer::setPreset(const std::string& name) {
    auto p = createPreset(name);
    if (!p) return false;
    preset_ = std::move(p);
    preset_name_ = name;
    // Start the trail buffer black: a frame-clearing preset leaves
    // prev_ stale, and a trail preset switching in would otherwise
    // decay one ghost frame of it.
    std::fill(prev_.begin(), prev_.end(), 0xFF000000);
    return true;
}

void VizRenderer::cyclePreset(int dir) {
    auto names = presetNames();
    if (names.size() < 2) return;
    int idx = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == preset_name_) { idx = static_cast<int>(i); break; }
    }
    int n = static_cast<int>(names.size());
    setPreset(names[((idx + dir) % n + n) % n]);
}

void VizRenderer::onTap(int x, int y) {
    if (preset_) preset_->onTap(x, y);
}

void VizRenderer::onLongPress(int x, int y) {
    if (preset_) preset_->onLongPress(x, y);
}

void VizRenderer::feedSamples(const float* samples, size_t count, int sample_rate) {
    sample_rate_ = sample_rate;
    if (count >= FFT_SIZE) {
        // Keep only the newest FFT_SIZE samples
        std::memcpy(window_.data(), samples + (count - FFT_SIZE),
                    FFT_SIZE * sizeof(float));
        window_fill_ = FFT_SIZE;
    } else {
        if (window_fill_ + count > FFT_SIZE) {
            size_t drop = window_fill_ + count - FFT_SIZE;
            std::memmove(window_.data(), window_.data() + drop,
                         (window_fill_ - drop) * sizeof(float));
            window_fill_ -= drop;
        }
        std::memcpy(window_.data() + window_fill_, samples, count * sizeof(float));
        window_fill_ += count;
    }
    analyze();
}

void VizRenderer::analyze() {
    if (!fft_plan_ || window_fill_ < FFT_SIZE) return;

    // Hann window into the FFT input
    for (int i = 0; i < FFT_SIZE; ++i) {
        double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / FFT_SIZE));
        fft_in_[i] = window_[i] * w;
    }
    fftw_execute(fft_plan_);

    const int fft_bins = FFT_SIZE / 2 + 1;
    const double f_min = 30.0;
    const double f_max = std::min(sample_rate_ / 2.0, 16000.0);
    const double log_span = std::log(f_max / f_min);

    float frame_max = 0.0f;
    for (int i = 0; i < num_bars_; ++i) {
        double f0 = f_min * std::exp(log_span * i / num_bars_);
        double f1 = f_min * std::exp(log_span * (i + 1) / num_bars_);
        int b0 = std::max(1, std::min(fft_bins - 1, static_cast<int>(f0 / sample_rate_ * FFT_SIZE)));
        int b1 = std::max(b0 + 1, std::min(fft_bins, static_cast<int>(f1 / sample_rate_ * FFT_SIZE)));

        double sum = 0.0;
        for (int b = b0; b < b1; ++b) {
            double re = fft_out_[b][0];
            double im = fft_out_[b][1];
            sum += std::sqrt(re * re + im * im);
        }
        mags_[i] = static_cast<float>(sum / (b1 - b0));
        if (mags_[i] > frame_max) frame_max = mags_[i];
    }

    // Auto-gain: fast attack, slow decay peak tracker
    peak_ = std::max(peak_ * 0.995f, frame_max);
    if (peak_ > 1e-6f) {
        for (auto& m : mags_) m = std::min(1.0f, m / peak_);
    }

    // Beat detection: low-band energy vs its running average
    int low_cut = std::max(1, static_cast<int>(
        num_bars_ * std::log(150.0 / f_min) / log_span));
    float low_e = 0.0f;
    for (int i = 0; i < low_cut && i < num_bars_; ++i) low_e += mags_[i];
    low_e /= low_cut;

    ++frames_since_beat_;
    if (low_e > 1.6f * energy_avg_ && low_e > 0.03f && frames_since_beat_ > 12) {
        pulse_ = 1.0f;
        frames_since_beat_ = 0;
    }
    energy_avg_ = 0.95f * energy_avg_ + 0.05f * low_e;

    for (int i = 0; i < num_bars_; ++i)
        smoothed_[i] = smoothed_[i] * (1.0f - smoothing_) + mags_[i] * smoothing_;
}

void VizRenderer::feedbackPass(Framebuffer& fb) {
    const float cx = w_ / 2.0f;
    const int decay_q = static_cast<int>(trail_decay_ * 256.0f);

    for (int y = 0; y < h_; ++y) {
        const uint32_t* prev_row = prev_.data() + static_cast<size_t>(y) * w_;
        for (int x = 0; x < w_; ++x) {
            int sx = static_cast<int>(cx + (x - cx) / trail_zoom_) - trail_drift_;
            uint32_t c = 0xFF000000;
            if (sx >= 0 && sx < w_) {
                uint32_t p = prev_row[sx];
                unsigned r = ((p >> 16) & 0xFF) * decay_q >> 8;
                unsigned g = ((p >> 8) & 0xFF) * decay_q >> 8;
                unsigned b = (p & 0xFF) * decay_q >> 8;
                c = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
            fb.setPixel(x, y, c);
        }
    }
}

void VizRenderer::render(Framebuffer& fb) {
    // Frame-clearing presets overwrite every pixel below, so the
    // decay pass and the previous-frame copy are wasted work.
    const bool clears = preset_ && preset_->clearsFrame();
    if (!clears) feedbackPass(fb);

    if (preset_)
        preset_->draw(*this, fb);

    pulse_ *= 0.90f;
    hue_ = std::fmod(hue_ + hue_speed_ + 360.0f, 360.0f);

    if (!clears)
        std::memcpy(prev_.data(), fb.data(),
                    static_cast<size_t>(w_) * h_ * sizeof(uint32_t));
}
