#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/font.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// "matrix" — slow falling code. A grid of glyph cells: each drop lights
// the cell under its head, and lit cells fade out on their own, which
// gives every drop a trail for free. Deliberately calm: music only adds
// a few extra drops on the beat, nothing flashes.
class MatrixPreset : public VizPreset {
public:
    const char* name() const override { return "matrix"; }
    bool clearsFrame() const override { return true; }

    void onTap(int x, int) override {
        for (int i = -1; i <= 1; ++i) startDrop(x / kPitchX + i * 2);
    }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        const int w = fb.width(), h = fb.height();
        if (w != w_ || h != h_) resize(w, h);
        fb.clear(0xFF000000);

        const bool beat = viz.pulse() > 0.95f && frame_ - last_beat_ > 20;
        if (beat) {
            last_beat_ = frame_;
            for (int i = 0; i < 4; ++i) startDrop(rnd(cols_));
        }
        if (rnd(100) < 30) startDrop(rnd(cols_));  // steady drizzle

        for (Drop& d : drops_) {
            if (d.row >= rows_ || --d.wait > 0) continue;
            d.wait = d.delay;
            if (++d.row < rows_) {
                Cell& c = cell(d.col, d.row);
                c.glyph = randomGlyph();
                c.level = 1.0f;
            }
        }
        drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                    [this](const Drop& d) { return d.row >= rows_; }),
                     drops_.end());

        const BitmapFont& font = font_small();
        for (int r = 0; r < rows_; ++r) {
            for (int c = 0; c < cols_; ++c) {
                Cell& cl = cell(c, r);
                if (cl.level < 0.04f) continue;
                if (rnd(900) == 0) cl.glyph = randomGlyph();  // code shimmer
                // Fresh cells read near-white, trails settle into green.
                const uint32_t color = cl.level > 0.92f
                    ? hsv_to_rgb(125.0f, 0.25f, 1.0f)
                    : hsv_to_rgb(125.0f, 0.90f, 0.32f + 0.68f * cl.level);
                font.drawChar(fb, c * kPitchX, r * kPitchY, cl.glyph, color);
                cl.level *= kFade;
            }
        }
        ++frame_;
    }

private:
    static constexpr int kPitchX = 8;
    static constexpr int kPitchY = 10;
    static constexpr float kFade = 0.983f;  // about two seconds to dark

    struct Cell { char glyph = ' '; float level = 0.0f; };
    struct Drop { int col, row, delay, wait; };

    Cell& cell(int c, int r) { return cells_[r * cols_ + c]; }

    int rnd(int n) {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<int>((seed_ >> 8) % static_cast<uint32_t>(n));
    }

    char randomGlyph() {
        static const char set[] = "0123456789ABCDEF<>/\\|=+-*:;.$#%";
        return set[rnd(static_cast<int>(sizeof(set)) - 1)];
    }

    void resize(int w, int h) {
        w_ = w;
        h_ = h;
        cols_ = w / kPitchX;
        rows_ = h / kPitchY;
        cells_.assign(static_cast<size_t>(cols_) * rows_, Cell{});
        drops_.clear();
    }

    void startDrop(int col) {
        if (col < 0 || col >= cols_) return;
        const int delay = 7 + rnd(10);  // frames per row
        drops_.push_back({col, -1, delay, 1});
    }

    int w_ = 0, h_ = 0, cols_ = 0, rows_ = 0;
    std::vector<Cell> cells_;
    std::vector<Drop> drops_;
    int frame_ = 0, last_beat_ = -100;
    uint32_t seed_ = 0x6d617478u;
};

REGISTER_PRESET("matrix", MatrixPreset);
