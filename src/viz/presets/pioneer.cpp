#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "viz/lkd.hpp"
#include "viz/pioneer_hues.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include "util/log.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

// "pioneer" — the actual Pioneer OEL display animations, decoded from
// the head units' own `.lkd` clip data (see src/viz/lkd.cpp for the
// format). Sources: the carrozzeria movie set (movieN, 256x64) plus
// the DEH-7500MP OEL Screensaver Studio set (named movies + BGV
// backgrounds, 128x33; li_* level indicators, 64x66).
//
// The wall is PAGED: page 0 is the carrozzeria movies (the iconic
// dolphin clip leads), page 1 the 7500MP movies, page 2 the BGV
// backgrounds, and the last page is the level indicators' own wall.
// Pages never rotate on their own — a LONG PRESS on the viz zone
// flips to the next page. Tapping a panel focuses that clip, any tap
// in focus mode returns to the wall.
//
// Level indicators work like they did on the head units. An li_*
// frame is 64x66 = two 64x33 meters stacked (left channel on top,
// right below); the head unit showed them SIDE BY SIDE, so each panel
// draws the pair at native 128x33 and tiles it across its width —
// scaled/stretched meters are unreadable at this size. The 30 frames
// are two identical 15-frame level ramps (0-14, 15-29); the displayed
// frame is picked from the live audio level, so instead of looping,
// each meter is a VU: left half meters this panel's frequency band,
// right half the next band up, with fast attack / slow decay
// ballistics. Silence parks every meter at level 0.
//
// Movie panels loop independently (phase-offset so the wall doesn't
// move in lockstep) at native aspect. Focus mode: a movie gets a
// centered native-aspect window in the accent color over a full-bleed
// stretched backdrop in full-bright stock color; an indicator tiles
// full-width in the accent color (no backdrop — the stretched version
// is unreadable). Both hues are live-adjustable from the "colors"
// mode; the main hue also retints the whole OEL chrome.
//
// Movie playback is not audio-driven (the originals just loop); beats
// only flash the phosphor brightness, silence dims it slightly.
//
// Clips are looked up in $TOUCHBAR_FX_CLIP_DIR, then
// /usr/local/share/touchbar-fx/clips, then ./assets/clips (dev/dump
// runs from the repo root). With no clips found the preset shows a
// dim notice instead of failing, so preset rotation keeps working.
class PioneerPreset : public VizPreset {
public:
    const char* name() const override { return "pioneer"; }

    // Clears every frame — skipping the trail pass is a big win on
    // the full-width focus backdrop.
    bool clearsFrame() const override { return true; }

    // Wall mode: focus the tapped panel's clip. Focus mode: any tap
    // returns to the wall.
    void onTap(int x, int) override {
        if (focused_ >= 0) {
            focused_ = -1;
            return;
        }
        if (pages_.empty() || w_ <= 0) return;
        const auto& page = pages_[static_cast<size_t>(page_)];
        const int n = static_cast<int>(page.size());
        if (n == 0) return;
        const int panel = std::max(0, std::min(n - 1, x * n / w_));
        focused_ = page[static_cast<size_t>(panel)].flat;
    }

    // Long press: flip the wall to the next page (also drops any
    // focus). The only page control — the wall never auto-rotates.
    void onLongPress(int, int) override {
        if (pages_.empty()) return;
        focused_ = -1;
        page_ = (page_ + 1) % static_cast<int>(pages_.size());
    }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        fb.clear(0xFF000000);
        ensureLoaded();
        if (pages_.empty()) {
            if (!warned_) {
                warned_ = true;
                log_warn("pioneer preset: no .lkd clips found "
                         "(looked in $TOUCHBAR_FX_CLIP_DIR, %s, ./assets/clips)",
                         kSystemClipDir);
            }
            fb.drawText(8, fb.height() / 2 - 6, "pioneer: no .lkd clips",
                        0xFF2A5A66, FontSize::Normal);
            return;
        }

        // Source playback at 14 fps, time-based: the render core's
        // draw rate sags under load, and counting draw calls would
        // slow the movies with it. A float accumulator keeps the
        // long-term rate exact. Loud tracks ease the rate up to +35%
        // — noticeable, not jarring.
        float speed = 1.0f;
        if (!viz.silent()) {
            const auto& bars = viz.bars();
            float sum = 0.0f;
            for (float v : bars) sum += std::sqrt(std::max(0.0f, v));
            const float e = std::min(
                1.0f, sum / std::max<size_t>(1, bars.size()) * 1.4f);
            speed += 0.35f * e;
        }
        const auto now = std::chrono::steady_clock::now();
        if (last_adv_.time_since_epoch().count() == 0) last_adv_ = now;
        frame_acc_ += std::chrono::duration<float>(now - last_adv_).count()
                      * speed;
        last_adv_ = now;
        while (frame_acc_ >= kFrameSeconds) {
            frame_acc_ -= kFrameSeconds;
            ++anim_frame_;
        }

        // Phosphor LUTs for this frame's brightness: the originals run
        // near full brightness always, so the base stays high; beats
        // flash it, silence only settles it slightly. Hues come from
        // the shared pioneerHues() so the "colors" mode can retune
        // them live: full-bright stock for the wall AND the focus
        // backdrop, full-bright accent for the focus window.
        const PioneerHues& hues = pioneerHues();
        const float pulse = viz.pulse();
        const float base = viz.silent() ? 0.72f : 0.85f;
        const float bright = std::min(1.0f, base + 0.20f * pulse);
        uint32_t lut[256], lut_accent[256];
        for (int i = 0; i < 256; ++i) {
            const float v = static_cast<float>(i) / 255.0f;
            lut[i] = hue_rgb(hues.main, 0.90f, bright * v);
            lut_accent[i] = hue_rgb(hues.accent, 0.95f, bright * v);
        }

        const int w = fb.width();
        const int h = fb.height();
        w_ = w;
        if (focused_ >= static_cast<int>(clips_.size())) focused_ = -1;

        if (focused_ >= 0) {
            const Panel& p = panels_[static_cast<size_t>(focused_)];
            if (p.indicator) {
                // Tiled full-width in the accent color — a stretched
                // meter is unreadable, so no backdrop here.
                drawIndicator(viz, fb, lut_accent, p, 0, w + kGutter, h);
            } else {
                drawClip(fb, lut, lut_accent, p, movieFrame(p));
            }
            return;
        }

        const auto& page = pages_[static_cast<size_t>(page_)];
        const int n = static_cast<int>(page.size());
        for (int i = 0; i < n; ++i) {
            const Panel& p = page[static_cast<size_t>(i)];
            const int x0 = i * w / n;
            const int x1 = (i + 1) * w / n;
            if (p.indicator)
                drawIndicator(viz, fb, lut, p, x0, x1, h);
            else
                drawPanel(fb, lut, p, movieFrame(p), x0, x1, h);
        }
    }

private:
    struct Panel {
        int flat;              // index into clips_
        bool indicator;        // li_* level indicator clip
        int band;              // panel position within its page
        int bands;             // panels on its page
    };

    struct VU { float l = 0.0f, r = 0.0f; };  // per-clip meter levels

    static constexpr int kGutter = 2;    // px of black between panels
    static constexpr int kMinMargin = 3; // px headroom when panels are wide
    static constexpr int kPhaseStep = 7; // source frames between panels
    static constexpr float kFrameSeconds = 1.0f / 14.0f;  // 14 fps playback
    static constexpr const char* kSystemClipDir =
        "/usr/local/share/touchbar-fx/clips";

    static bool isIndicator(const LkdClip& c) {
        return c.name.size() > 3 && c.name.compare(0, 3, "li_") == 0;
    }

    // Movies follow the shared phase-offset animation clock.
    size_t movieFrame(const Panel& p) const {
        const LkdClip& c = clips_[static_cast<size_t>(p.flat)];
        return static_cast<size_t>(
            (anim_frame_ + p.flat * kPhaseStep)
            % static_cast<long>(c.frames.size()));
    }

    // Indicator frame for one channel: live band level onto the
    // clip's 15-frame ramp. Left meters the panel's own band (ramp
    // A), right the next band up (ramp B) — the two halves of the
    // stereo pair the head unit showed side by side. VU ballistics:
    // fast attack, slow decay.
    size_t levelFrame(const VizRenderer& viz, const Panel& p,
                      int channel) {
        const LkdClip& c = clips_[static_cast<size_t>(p.flat)];
        VU& v = vu_[static_cast<size_t>(p.flat)];
        float& lv = channel ? v.r : v.l;
        const int band = (p.band + channel) % p.bands;
        const float target = bandEnergy(viz, band, p.bands);
        lv += (target - lv) * (target > lv ? 0.45f : 0.08f);
        const size_t half = c.frames.size() / 2;
        if (half == 0) return 0;
        size_t lvl = static_cast<size_t>(lv * static_cast<float>(half));
        if (lvl >= half) lvl = half - 1;
        return (channel ? half : 0) + lvl;
    }

    // Sqrt-mean of a spectrum slice (pushes the quiet end up, like
    // the penguin preset's energy), 0 when silent.
    static float bandEnergy(const VizRenderer& viz, int band, int bands) {
        if (viz.silent()) return 0.0f;
        const auto& bars = viz.bars();
        if (bars.empty()) return 0.0f;
        const size_t lo = bars.size() * band / bands;
        const size_t hi = std::max(lo + 1, bars.size() * (band + 1) / bands);
        float sum = 0.0f;
        for (size_t i = lo; i < hi; ++i)
            sum += std::sqrt(std::max(0.0f, bars[i]));
        return std::min(1.0f, sum / (hi - lo) * 1.5f);
    }

    // One movie wall panel: native aspect, centered in its slice.
    void drawPanel(Framebuffer& fb, const uint32_t* lut, const Panel& p,
                   size_t fi, int x0, int x1, int h) {
        const LkdClip& c = clips_[static_cast<size_t>(p.flat)];
        const int inner_w = x1 - x0 - kGutter;
        if (inner_w <= 0) return;
        const int ch = std::min(inner_w * c.height / c.width,
                                h - 2 * kMinMargin);
        if (ch <= 0) return;
        const int y0 = (h - ch) / 2;
        const int cx = x0 + (x1 - x0 - inner_w) / 2;
        blit(fb, lut, c, fi, cx, y0, inner_w, ch);
    }

    // Level indicator panel: split the stacked 64x66 frame into its
    // two 64x33 meters (left = top half, right = bottom half) and
    // tile the side-by-side pair across the slice at native scale,
    // like the head unit did. No scaling — stretched meters are
    // unreadable at this size.
    void drawIndicator(const VizRenderer& viz, Framebuffer& fb,
                       const uint32_t* lut,
                       const Panel& p, int x0, int x1, int h) {
        const LkdClip& c = clips_[static_cast<size_t>(p.flat)];
        const int mh = c.height / 2;
        if (mh <= 0) return;
        const size_t fl = levelFrame(viz, p, 0);
        const size_t fr = levelFrame(viz, p, 1);
        const int y0 = (h - mh) / 2;
        const int right = x1 - kGutter;
        int x = x0;
        while (x < right) {
            blitHalf(fb, lut, c, fl, 0, x, y0,
                     std::min(c.width, right - x), mh);
            x += c.width;
            if (x >= right) break;
            blitHalf(fb, lut, c, fr, 1, x, y0,
                     std::min(c.width, right - x), mh);
            x += c.width;
        }
    }

    // Focus mode for a movie: full-bleed stretched backdrop in the
    // stock color behind a centered, native-aspect accent window.
    void drawClip(Framebuffer& fb, const uint32_t* lut_bg,
                  const uint32_t* lut_win, const Panel& p, size_t fi) {
        const LkdClip& c = clips_[static_cast<size_t>(p.flat)];
        const int w = fb.width();
        const int h = fb.height();
        blit(fb, lut_bg, c, fi, 0, 0, w, h);
        const int ch = h - 2 * kMinMargin;
        const int cw = ch * c.width / c.height;
        const int x0 = (w - cw) / 2;
        const int y0 = kMinMargin;
        fb.fillRect(x0 - 2, y0 - 2, cw + 4, ch + 4, 0xFF000000);
        blit(fb, lut_win, c, fi, x0, y0, cw, ch);
    }

    // Nearest-neighbor blit of one clip frame into a destination box.
    // Source indices advance Bresenham-style — no per-pixel division.
    static void blit(Framebuffer& fb, const uint32_t* lut,
                     const LkdClip& c, size_t fi,
                     int dx, int dy, int dw, int dh) {
        const auto& frame = c.frames[fi];
        int sy = 0, yacc = 0;
        for (int y = 0; y < dh; ++y) {
            const uint8_t* src_row =
                frame.data() + static_cast<size_t>(
                    std::min(c.height - 1, sy)) * c.width;
            int sx = 0, xacc = 0;
            for (int x = 0; x < dw; ++x) {
                const uint8_t lum = src_row[std::min(c.width - 1, sx)];
                if (lum > 8) fb.setPixel(dx + x, dy + y, lut[lum]);
                xacc += c.width;
                while (xacc >= dw) { ++sx; xacc -= dw; }
            }
            yacc += c.height;
            while (yacc >= dh) { ++sy; yacc -= dh; }
        }
    }

    // 1:1 blit of one meter half (0 = top, 1 = bottom) of an
    // indicator frame — no scaling, just clipped to dw.
    static void blitHalf(Framebuffer& fb, const uint32_t* lut,
                         const LkdClip& c, size_t fi, int half,
                         int dx, int dy, int dw, int mh) {
        const auto& frame = c.frames[fi];
        const int sy0 = half * mh;
        for (int y = 0; y < mh; ++y) {
            const uint8_t* src_row =
                frame.data() + static_cast<size_t>(sy0 + y) * c.width;
            for (int x = 0; x < dw; ++x) {
                const uint8_t lum = src_row[x];
                if (lum > 8) fb.setPixel(dx + x, dy + y, lut[lum]);
            }
        }
    }

    void ensureLoaded() {
        if (loaded_) return;
        loaded_ = true;

        std::vector<std::string> dirs;
        if (const char* env = std::getenv("TOUCHBAR_FX_CLIP_DIR"))
            dirs.emplace_back(env);
        dirs.emplace_back(kSystemClipDir);
        dirs.emplace_back("assets/clips");

        for (const std::string& dir : dirs) {
            clips_ = loadLkdDir(dir);
            if (!clips_.empty()) {
                log_info("pioneer preset: loaded %zu clips from %s",
                         clips_.size(), dir.c_str());
                break;
            }
        }
        if (clips_.empty()) return;

        // Group order: carrozzeria movies, 7500MP movies, BGV
        // backgrounds, then the level indicators' own page.
        auto rank = [](const LkdClip& c) {
            if (isIndicator(c)) return 3;
            if (c.name.compare(0, 3, "bgv") == 0) return 2;
            if (c.name.compare(0, 5, "movie") == 0) return 0;
            return 1;
        };
        std::stable_sort(clips_.begin(), clips_.end(),
                         [&](const LkdClip& a, const LkdClip& b) {
                             return rank(a) < rank(b);
                         });

        // The dolphin clip is the iconic one — first panel of page 0.
        for (size_t i = 0; i < clips_.size(); ++i) {
            if (clips_[i].name == "movie8_f") {
                std::swap(clips_[i], clips_[0]);
                break;
            }
        }

        vu_.assign(clips_.size(), VU{});
        panels_.reserve(clips_.size());
        for (size_t i = 0; i < clips_.size(); ++i)
            panels_.push_back({static_cast<int>(i), isIndicator(clips_[i]),
                               0, 0});

        // Paginate: one page per group (groups are contiguous after
        // the sort).
        size_t i = 0;
        while (i < clips_.size()) {
            const int r = rank(clips_[i]);
            std::vector<Panel> page;
            size_t j = i;
            for (; j < clips_.size() && rank(clips_[j]) == r; ++j) {
                Panel p = panels_[j];
                p.band = static_cast<int>(j - i);
                page.push_back(p);
            }
            for (auto& p : page) {
                p.bands = static_cast<int>(page.size());
                panels_[static_cast<size_t>(p.flat)] = p;
            }
            pages_.push_back(page);
            i = j;
        }
        log_info("pioneer preset: %zu pages", pages_.size());

        // Debug/dump hook: start on a specific wall page.
        if (const char* e = std::getenv("TOUCHBAR_FX_PIONEER_PAGE")) {
            const int p = std::atoi(e);
            if (p >= 0 && p < static_cast<int>(pages_.size())) page_ = p;
        }
    }

    bool loaded_ = false;
    bool warned_ = false;
    std::vector<LkdClip> clips_;
    std::vector<Panel> panels_;   // per flat clip index
    std::vector<VU> vu_;          // per flat clip: smoothed L/R levels
    std::vector<std::vector<Panel>> pages_;
    int page_ = 0;
    long anim_frame_ = 0;
    float frame_acc_ = 0.0f;  // time-based 14 fps playback accumulator
    std::chrono::steady_clock::time_point last_adv_{};
    int w_ = 0;        // last drawn width, for tap hit-testing
    int focused_ = -1; // flat clip index, -1 = wall mode
};

REGISTER_PRESET("pioneer", PioneerPreset);
