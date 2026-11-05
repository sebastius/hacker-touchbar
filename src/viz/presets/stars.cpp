#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// "stars" — three layers of stars drifting sideways, far ones slowest.
// A strong beat sends one shooting star across; so does a tap.
class StarsPreset : public VizPreset {
public:
    const char* name() const override { return "stars"; }
    bool clearsFrame() const override { return true; }

    void onTap(int x, int y) override { launch(static_cast<float>(x), y); }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        const int w = fb.width(), h = fb.height();
        if (w != w_ || h != h_) resize(w, h);
        fb.clear(0xFF000000);

        if (viz.pulse() > 0.95f && ++beats_ % 6 == 0)
            launch(static_cast<float>(rnd(w)), rnd(h / 2));

        for (Star& s : stars_) {
            s.x -= 0.05f + 0.11f * s.layer;
            if (s.x < 0) {
                s.x += w;
                s.y = rnd(h);
            }
            const int size = s.layer == 2 ? 2 : 1;
            const float v = 0.25f + 0.32f * s.layer;
            fb.fillRect(static_cast<int>(s.x), s.y, size, size,
                        hsv_to_rgb(125.0f, 0.35f, v));
        }

        for (Meteor& m : meteors_) {
            m.x += 5.0f;
            m.y += 0.8f;
            m.life -= 0.02f;
            for (int i = 0; i < 16; ++i)
                fb.fillRect(static_cast<int>(m.x) - i * 4,
                            static_cast<int>(m.y - i * 0.64f), 2, 2,
                            hsv_to_rgb(125.0f, 0.30f,
                                       m.life * (1.0f - i / 16.0f)));
        }
        meteors_.erase(std::remove_if(meteors_.begin(), meteors_.end(),
                                      [](const Meteor& m) { return m.life <= 0; }),
                       meteors_.end());
    }

private:
    struct Star { float x; int y; int layer; };
    struct Meteor { float x, y, life; };

    int rnd(int n) {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<int>((seed_ >> 8) % static_cast<uint32_t>(n));
    }

    void resize(int w, int h) {
        w_ = w;
        h_ = h;
        stars_.clear();
        for (int i = 0; i < w / 6; ++i)
            stars_.push_back({static_cast<float>(rnd(w)), rnd(h), rnd(3)});
    }

    void launch(float x, int y) {
        if (meteors_.size() < 3)
            meteors_.push_back({x, static_cast<float>(y), 1.0f});
    }

    int w_ = 0, h_ = 0, beats_ = 0;
    std::vector<Star> stars_;
    std::vector<Meteor> meteors_;
    uint32_t seed_ = 0x73746172u;
};

REGISTER_PRESET("stars", StarsPreset);
