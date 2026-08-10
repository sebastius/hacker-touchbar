#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <algorithm>
#include <cmath>

// "oscilloscope" — raw waveform trace over the feedback trails.
class OscilloscopePreset : public VizPreset {
public:
    const char* name() const override { return "oscilloscope"; }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        const int w = fb.width();
        const int h = fb.height();
        const int cy = h / 2;
        const float pulse = viz.pulse();
        const float gain = 0.75f + 0.6f * pulse;
        uint32_t c = hsv_to_rgb(std::fmod(viz.hue() + pulse * 40.0f, 360.0f),
                                0.9f, std::min(1.0f, gain));

        const auto& window = viz.wave();
        const size_t fill = viz.waveFill();
        for (int x = 0; x < w; ++x) {
            size_t idx = static_cast<size_t>(x) * (fill - 1) / std::max(1, w - 1);
            if (idx >= fill) idx = fill - 1;
            float s = fill ? window[idx] : 0.0f;
            int y = cy - static_cast<int>(s * (cy - 2));
            fb.setPixel(x, y, c);
            if (y + 1 < h) fb.setPixel(x, y + 1, c);
        }
    }
};

REGISTER_PRESET("oscilloscope", OscilloscopePreset);
