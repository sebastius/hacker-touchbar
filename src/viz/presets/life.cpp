#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <cstdint>
#include <vector>

// "life" — Conway's Game of Life on a wrapping grid, stepping a few
// times a second. Dead cells fade instead of vanishing, so the patterns
// leave soft afterglow. Beats seed a small cluster; so does a tap.
class LifePreset : public VizPreset {
public:
    const char* name() const override { return "life"; }
    bool clearsFrame() const override { return true; }

    void onTap(int x, int) override { seedAt(x / kCell, rows_ / 2); }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        if (fb.width() != w_ || fb.height() != h_) resize(fb.width(), fb.height());
        fb.clear(0xFF000000);

        if (viz.pulse() > 0.95f && frame_ - last_beat_ > 45) {
            last_beat_ = frame_;
            seedAt(rnd(cols_), rnd(rows_));
        }
        if (++frame_ % kStepFrames == 0) step();

        for (int r = 0; r < rows_; ++r) {
            for (int c = 0; c < cols_; ++c) {
                const size_t i = idx(c, r);
                glow_[i] = alive_[i] ? 1.0f : glow_[i] * 0.93f;
                if (glow_[i] < 0.05f) continue;
                fb.fillRect(c * kCell, r * kCell, kCell - 1, kCell - 1,
                            hsv_to_rgb(125.0f, 0.85f, 0.12f + 0.80f * glow_[i]));
            }
        }
    }

private:
    static constexpr int kCell = 5;
    static constexpr int kStepFrames = 14;  // about four generations a second

    size_t idx(int c, int r) const {
        return static_cast<size_t>(((r + rows_) % rows_) * cols_ + (c + cols_) % cols_);
    }

    int rnd(int n) {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<int>((seed_ >> 8) % static_cast<uint32_t>(n));
    }

    void resize(int w, int h) {
        w_ = w;
        h_ = h;
        cols_ = w / kCell;
        rows_ = h / kCell;
        alive_.assign(static_cast<size_t>(cols_) * rows_, 0);
        next_ = alive_;
        glow_.assign(alive_.size(), 0.0f);
        for (size_t i = 0; i < alive_.size(); ++i) alive_[i] = rnd(100) < 22;
    }

    void seedAt(int c, int r) {
        for (int i = 0; i < 14; ++i)
            alive_[idx(c + rnd(7) - 3, r + rnd(7) - 3)] = 1;
    }

    void step() {
        size_t population = 0;
        for (int r = 0; r < rows_; ++r) {
            for (int c = 0; c < cols_; ++c) {
                int n = 0;
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc)
                        if ((dr || dc) && alive_[idx(c + dc, r + dr)]) ++n;
                const size_t i = idx(c, r);
                next_[i] = n == 3 || (alive_[i] && n == 2);
                population += next_[i];
            }
        }
        alive_.swap(next_);
        // Keep the colony going once it thins out or settles.
        if (population < alive_.size() / 25 || rnd(12) == 0)
            seedAt(rnd(cols_), rnd(rows_));
    }

    int w_ = 0, h_ = 0, cols_ = 0, rows_ = 0;
    std::vector<uint8_t> alive_, next_;
    std::vector<float> glow_;
    int frame_ = 0, last_beat_ = -100;
    uint32_t seed_ = 0x6c696665u;
};

REGISTER_PRESET("life", LifePreset);
