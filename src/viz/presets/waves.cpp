#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <algorithm>
#include <cmath>

// "waves" — the classic look: mirrored spectrum bars over the
// feedback trails, beat-reactive brightness/hue.
class WavesPreset : public VizPreset {
public:
    const char* name() const override { return "waves"; }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        const int w = fb.width();
        const int h = fb.height();
        const auto& smoothed = viz.bars();
        const int num = std::min(viz.barCount(), w / 2);
        if (num <= 0) return;
        const int bar_w = std::max(1, w / num);
        const int cy = h / 2;
        const float pulse = viz.pulse();
        const float gain = 0.75f + 0.6f * pulse;

        for (int i = 0; i < num; ++i) {
            float v = std::sqrt(std::max(0.0f, smoothed[i]));
            int bh = static_cast<int>(v * cy);
            if (bh < 1 && smoothed[i] > 0.01f) bh = 1;
            if (bh > cy) bh = cy;

            float hue = viz.hue() + pulse * 40.0f + (30.0f * i / num);
            int x = i * w / num;

            for (int r = 0; r < bh; ++r) {
                float tip = (bh > 1) ? static_cast<float>(r) / (bh - 1) : 1.0f;
                float bright = gain * (0.55f + 0.45f * tip);
                if (bright > 1.0f) bright = 1.0f;
                uint32_t c = hsv_to_rgb(std::fmod(hue, 360.0f), 0.9f, bright);
                fb.drawHLine(x, cy - 1 - r, bar_w, c);
                fb.drawHLine(x, cy + r, bar_w, c);
            }
        }

        // Idle: a faint center line so the bar never looks dead
        if (viz.silent())
            fb.drawHLine(0, cy, w, hsv_to_rgb(viz.hue(), 0.9f, 0.15f));
    }
};

REGISTER_PRESET("waves", WavesPreset);
