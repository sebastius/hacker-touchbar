#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

// "spectrum" — the Pioneer DEH OEL segmented analyzer: columns of
// discrete 2px segments with 1px gaps (not continuous bars like
// "waves"), plus a classic peak-hold dot per column that holds
// ~0.7 s after a hit and then steps down slowly. Fixed Pioneer cyan
// on black (same family as "dolphins"), the top two lit segments of
// each column and the peak dot run whiter. No feedback trails — the
// frame is cleared for a crisp, slightly flickery-fast read.
class SpectrumPreset : public VizPreset {
public:
    const char* name() const override { return "spectrum"; }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        const int w = fb.width();
        const int h = fb.height();
        fb.clear(0xFF000000);

        const auto& smoothed = viz.bars();
        const int num = std::min(viz.barCount(), w / COL_PITCH);
        if (num <= 0) return;
        const int num_segs = h / SEG_PITCH;
        if (num_segs < 2) return;

        peak_.resize(num, 0.0f);
        hold_.resize(num, 0);

        const float pulse = viz.pulse();
        const float gain = 0.70f + 0.30f * pulse;

        const uint32_t seg_on   = hsv_to_rgb(197.0f, 0.85f, std::min(1.0f, 0.55f + 0.35f * gain));
        const uint32_t seg_hot  = hsv_to_rgb(195.0f, 0.40f, 1.0f);   // top 2 lit
        const uint32_t peak_dot = hsv_to_rgb(195.0f, 0.25f, 1.0f);   // near-white

        for (int i = 0; i < num; ++i) {
            float v = std::sqrt(std::max(0.0f, smoothed[i]));
            int level = static_cast<int>(v * num_segs + 0.5f);
            if (level > num_segs) level = num_segs;

            // Peak hold: snap up instantly, hold ~42 frames, then step
            // down at ~3.5 segments/second.
            if (static_cast<float>(level) >= peak_[i]) {
                peak_[i] = static_cast<float>(level);
                hold_[i] = 42;
            } else if (hold_[i] > 0) {
                --hold_[i];
            } else {
                peak_[i] -= 0.06f;
                if (peak_[i] < 0.0f) peak_[i] = 0.0f;
            }

            const int x = i * COL_PITCH;
            for (int s = 0; s < level; ++s) {
                int y = h - (s + 1) * SEG_PITCH;
                uint32_t c = (s >= level - 2) ? seg_hot : seg_on;
                fb.drawHLine(x, y, COL_W, c);
                fb.drawHLine(x, y + 1, COL_W, c);
            }

            int ps = static_cast<int>(peak_[i] + 0.5f);
            if (ps > level && ps <= num_segs) {
                int y = h - ps * SEG_PITCH;
                fb.drawHLine(x, y, COL_W, peak_dot);
                fb.drawHLine(x, y + 1, COL_W, peak_dot);
            }
        }

        // Idle: a faint segmented baseline so the bar never looks dead
        if (viz.silent()) {
            uint32_t dim = hsv_to_rgb(197.0f, 0.85f, 0.15f);
            for (int i = 0; i < num; ++i) {
                fb.drawHLine(i * COL_PITCH, h - SEG_PITCH, COL_W, dim);
                fb.drawHLine(i * COL_PITCH, h - SEG_PITCH + 1, COL_W, dim);
            }
        }
    }

private:
    static constexpr int COL_PITCH = 6;  // 5px column + 1px gap
    static constexpr int COL_W = 5;
    static constexpr int SEG_PITCH = 3;  // 2px segment + 1px gap

    std::vector<float> peak_;  // peak level per column (float for smooth fall)
    std::vector<int>   hold_;  // frames left before the peak may fall
};

REGISTER_PRESET("spectrum", SpectrumPreset);
