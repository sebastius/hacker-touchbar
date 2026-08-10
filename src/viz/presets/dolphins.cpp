#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// "dolphins" — 2000s Pioneer DEH head-unit OEL display, reimagined as
// a side-view fishtank: the whole viz zone is water, with an animated
// beat-brightened shimmer line across the top, sparse drifting
// plankton motes and a faint top-down light gradient. Dolphins roam
// the tank in 2D (velocity + heading, slow sinusoidal depth drift,
// gentle S-turns) and ease into smooth U-turns before touching the
// glass — they never leave the canvas. Bodies are generated
// procedurally every frame: a spine from beak tip to fluke notch
// carries a radius profile (beak, melon bulge, body taper, tail
// stock), a traveling-wave swim undulation (small at the head, large
// at the tail, frequency tied to swim speed) and a leap flex keyed to
// jump phase. Dolphins heading left are rendered facing right and
// mirrored per-pixel, so the dorsal fin always stays on the back.
// Beats launch surface breaches (arcs that clip out of the tank top,
// foam + droplets on breach and re-entry); big low-band hits fire
// synchronized pair leaps. Audio energy drives both swim speed and
// tail-beat frequency; silence is lazy drifting. The shared feedback
// trails are skipped (the frame is cleared instead): drift/zoom smear
// turns the scene to mush.
class DolphinsPreset : public VizPreset {
public:
    const char* name() const override { return "dolphins"; }

    DolphinsPreset() {
        // Initial pod (positions assigned on first draw, when the
        // framebuffer geometry is known) + plankton motes.
        for (int i = 0; i < 3; ++i) {
            Dolphin d;
            d.depth = 0.25f + 0.75f * (static_cast<float>(i) / 3.0f);
            d.phase = rnd() * 6.2832f;
            dolphins_.push_back(d);
        }
        for (int i = 0; i < 22; ++i) {
            Mote m;
            m.bx = rnd();
            m.by = 0.15f + 0.8f * rnd();
            m.spd = 0.05f + 0.20f * rnd();
            m.ph = rnd() * 6.2832f;
            motes_.push_back(m);
        }
    }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        const int w = fb.width();
        const int h = fb.height();
        u_ = h / 60.0f;  // geometry unit (designed at 60 px)
        h_ = h;
        surface_y_ = std::max(3, h * 8 / 100);  // shimmer line at the top

        // Monochrome Pioneer cyan — no trails, clean black every frame.
        fb.clear(0xFF000000);

        const float pulse = viz.pulse();
        energy_ = audioEnergy(viz);
        const float bass = lowBand(viz);
        ++frame_;

        for (size_t i = 0; i < dolphins_.size(); ++i) {
            Dolphin& d = dolphins_[i];
            if (!d.init) {
                d.init = true;
                d.len = (55.0f + 85.0f * d.depth) * u_;
                // Deterministic spread across the whole tank + jitter.
                const float fi = static_cast<float>(i)
                                 / static_cast<float>(dolphins_.size());
                d.x = w * (0.12f + 0.76f * fi) + (rnd() - 0.5f) * w * 0.08f;
                d.y = surface_y_ + 8.0f * u_
                      + (h - surface_y_ - 16.0f * u_) * (0.2f + 0.9f * fi)
                      + (rnd() - 0.5f) * 6.0f * u_;
                d.dir = (rnd() < 0.5f) ? 1.0f : -1.0f;
                d.heading = (d.dir > 0.0f) ? 0.0f : PI;
            }
        }

        // A fresh beat (pulse resets to 1.0, then decays) can launch a
        // breach. Big low-band hits sometimes launch a synchronized
        // pair (the iconic Pioneer shot).
        if (pulse > 0.95f && frame_ != last_beat_frame_) {
            last_beat_frame_ = frame_;
            const bool pair = bass > 0.5f && rnd() < 0.55f;
            int launched = 0;
            for (auto& d : dolphins_) {
                if (!canLeap(d, w, h)) continue;
                launch(d, bass, 1.0f);
                ++launched;
                if (!pair || launched == 2) break;
            }
        } else if (viz.silent() && rnd() < 0.0012f) {
            // Silence: lazy drifting, very rare lazy leaps.
            for (auto& d : dolphins_) {
                if (canLeap(d, w, h)) {
                    launch(d, 0.0f, 0.45f);
                    break;
                }
            }
        }

        drawWater(fb, pulse, w);

        // Far dolphins first so near ones overlap them.
        std::vector<Dolphin*> order;
        for (auto& d : dolphins_) order.push_back(&d);
        std::sort(order.begin(), order.end(),
                  [](const Dolphin* a, const Dolphin* b) { return a->depth < b->depth; });

        for (Dolphin* d : order) {
            d->len = (55.0f + 85.0f * d->depth) * u_;
            // Energy drives swim speed AND tail-undulation frequency:
            // faster swim = faster tail, which sells "alive". The base
            // speed must cross a ~1560 px tank in seconds, not minutes.
            d->speed = (0.9f + 1.6f * d->depth) * (0.55f + 1.3f * energy_) * u_;
            d->phase += 0.10f + 0.38f * energy_;

            if (d->leap_t >= 0.0f)
                updateLeap(*d, w);
            else
                roam(*d, w, h);

            drawDolphin(fb, *d, pulse);
        }

        updateFoam(fb);
        updateParticles(fb);
    }

private:
    // ---- dolphin model ----
    struct Dolphin {
        bool init = false;
        float x = 0.0f, y = 0.0f;   // spine anchor (center of mass), px
        float heading = 0.0f;       // rad, screen coords (y down)
        float dir = 1.0f;           // horizontal intent: +1 right, -1 left
        float depth = 0.5f;         // 0 = far (small/dim/slow), 1 = near
        float len = 80.0f;          // body length, px (set per frame)
        float speed = 0.0f;
        float phase = 0.0f;         // swim undulation phase
        float leap_t = -1.0f;       // <0 swimming, 0..1 mid-leap
        float leap_dur = 50.0f;
        float leap_h = 30.0f;
        float leap_y0 = 0.0f;       // anchor y frozen at launch
        float leap_dir = 1.0f;      // horizontal sign frozen at launch
    };

    struct Particle {
        float x, y, vx, vy, life, max_life;
    };

    struct Foam {
        float x, life, max_life, spread, grow;
    };

    struct Mote {
        float bx, by;   // base position, normalized 0..1
        float spd;      // horizontal drift, px/frame
        float ph;       // bob phase
    };

    // Shading tones (indices into the per-dolphin palettes).
    enum Tone { OUTLINE = 0, MID, BELLY, EDGE, FIN, SPEC, TONE_COUNT };

    static constexpr float PI = 3.14159265f;

    // Radius profile along the spine, t = 0 beak tip .. 1 fluke notch,
    // as a fraction of max girth. Dorsal carries the melon bulge at
    // t~0.14-0.20 and a slight dip behind it (the blowhole crease).
    static constexpr int PROF_N = 10;
    static constexpr float PROF_T[PROF_N]  = {0.00f, 0.06f, 0.12f, 0.18f, 0.26f,
                                              0.34f, 0.50f, 0.70f, 0.86f, 1.00f};
    static constexpr float PROF_D[PROF_N]  = {0.05f, 0.20f, 0.68f, 0.95f, 0.88f,
                                              1.00f, 0.88f, 0.55f, 0.28f, 0.07f};
    static constexpr float PROF_V[PROF_N]  = {0.05f, 0.16f, 0.45f, 0.70f, 0.85f,
                                              0.95f, 0.85f, 0.53f, 0.26f, 0.07f};

    static float profileAt(const float* table, float t) {
        for (int i = 1; i < PROF_N; ++i) {
            if (t <= PROF_T[i]) {
                const float f = (t - PROF_T[i - 1]) / (PROF_T[i] - PROF_T[i - 1]);
                return table[i - 1] + f * (table[i] - table[i - 1]);
            }
        }
        return table[PROF_N - 1];
    }

    static float wrapPi(float a) {
        while (a > PI) a -= 2.0f * PI;
        while (a < -PI) a += 2.0f * PI;
        return a;
    }

    // Deterministic LCG so dumps/tests are reproducible.
    float rnd() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<float>((seed_ >> 8) & 0xFFFFFF) / 16777216.0f;
    }

    static float audioEnergy(const VizRenderer& viz) {
        if (viz.silent()) return 0.0f;
        const auto& bars = viz.bars();
        float sum = 0.0f;
        for (float v : bars) sum += std::sqrt(std::max(0.0f, v));
        float e = sum / std::max<size_t>(1, bars.size());
        return std::min(1.0f, e * 1.4f);
    }

    // Low-band strength (kick/bass), used for leap height + pair leaps.
    static float lowBand(const VizRenderer& viz) {
        if (viz.silent()) return 0.0f;
        const auto& bars = viz.bars();
        const size_t k = std::max<size_t>(1, bars.size() / 12);
        float sum = 0.0f;
        for (size_t i = 0; i < k; ++i) sum += std::sqrt(std::max(0.0f, bars[i]));
        return std::min(1.0f, sum / k * 2.0f);
    }

    // ---- 2D roaming ----

    // Steer the dolphin around the tank: slow sinusoidal depth drift,
    // gentle turns, and a smooth U-turn (through the vertical, toward
    // mid-tank) before it can touch the side glass. Top/bottom are
    // eased off the same way. Never leaves the canvas.
    void roam(Dolphin& d, int w, int h) {
        const float margin = d.len * 1.0f;

        // Side-glass pressure: flips the horizontal intent early, so
        // the heading blend below has room to arc the turn.
        float press = 0.0f;
        if (d.x < margin) press = (margin - d.x) / margin;
        else if (d.x > w - margin) press = -(d.x - (w - margin)) / margin;
        if (press > 0.5f && d.dir < 0.0f) d.dir = 1.0f;
        if (press < -0.5f && d.dir > 0.0f) d.dir = -1.0f;

        // Vertical intent: slow deep wander (per-dolphin phase offset
        // via depth) + top/bottom ease-off.
        const float top_m = surface_y_ + 6.0f * u_ + d.len * 0.15f;
        const float bot_m = h - 6.0f * u_ - d.len * 0.10f;
        float vert = 0.55f * std::sin(static_cast<float>(frame_) * 0.006f
                                      + d.phase * 0.3f + d.depth * 9.0f);
        if (d.y < top_m) vert += 1.0f * (top_m - d.y) / top_m;
        else if (d.y > bot_m) vert -= 1.2f * (d.y - bot_m) / (h * 0.25f);
        // During a U-turn, arc through the vertical direction that has
        // the most room (toward mid-tank).
        if (press != 0.0f)
            vert += ((d.y < h * 0.5f) ? 0.8f : -0.8f)
                    * std::min(1.0f, std::fabs(press));
        vert = std::max(-1.1f, std::min(1.1f, vert));

        // Desired heading: dir picks the horizontal half-plane, vert
        // the pitch (vert > 0 = downward in screen coords).
        const float des = (d.dir > 0.0f) ? vert : PI - vert;
        const float rate = 0.030f + 0.030f * energy_
                           + 0.05f * std::min(1.0f, std::fabs(press));
        const float dth = wrapPi(des - d.heading);
        d.heading = wrapPi(d.heading + std::max(-rate, std::min(rate, dth)));

        d.x += std::cos(d.heading) * d.speed;
        d.y += std::sin(d.heading) * d.speed * 0.75f
               + std::sin(d.phase * 0.5f) * 0.25f * u_;  // idle bob

        // Safety net: steering normally prevents ever getting here.
        // Anchor stays len/2+ inside so the nose never pokes the glass.
        d.x = std::max(d.len * 0.55f, std::min(w - d.len * 0.55f, d.x));
        d.y = std::max(static_cast<float>(surface_y_) + 2.0f,
                       std::min(static_cast<float>(h) - 3.0f, d.y));
    }

    // ---- leaps (surface breaches) ----

    bool canLeap(const Dolphin& d, int w, int h) const {
        return d.leap_t < 0.0f
               && d.y < surface_y_ + h * 0.30f       // near the surface
               && d.x > d.len * 0.7f && d.x < w - d.len * 0.7f  // arc room
               && std::fabs(std::sin(d.heading)) < 0.6f;  // not mid-turn
    }

    void launch(Dolphin& d, float bass, float scale) {
        d.leap_t = 0.0f;
        d.leap_dur = 42.0f + 18.0f * rnd() + 12.0f * (1.0f - energy_);
        // Clear the shimmer line, then some — clipping at the canvas
        // top is fine and reads as "out of the tank".
        d.leap_h = (d.y - surface_y_)
                   + (8.0f + 14.0f * d.depth) * u_ * scale * (0.6f + 0.8f * bass);
        d.leap_y0 = d.y;
        d.leap_dir = (std::cos(d.heading) >= 0.0f) ? 1.0f : -1.0f;
        d.dir = d.leap_dir;
        // Breach splash at the head (it clears the surface first).
        const float hx = d.x + std::cos(d.heading) * 0.4f * d.len;
        splash(hx, static_cast<float>(surface_y_),
               8 + static_cast<int>(8 * d.depth * (0.5f + bass)), d.speed * d.leap_dir);
        foam(hx, 0.7f + 0.6f * bass);
    }

    void updateLeap(Dolphin& d, int w) {
        const float lt = d.leap_t;
        // Ballistic arc: constant horizontal drift, parabolic height.
        // Heading follows the tangent (nose-up breach, level apex,
        // nose-down re-entry) with the horizontal sign preserved.
        const float dy = -d.leap_h * 4.0f * (1.0f - 2.0f * lt) / d.leap_dur;
        d.heading = std::atan2(dy, d.leap_dir * std::max(0.6f, d.speed));
        d.x += d.leap_dir * std::max(0.6f, d.speed);
        d.y = d.leap_y0 - d.leap_h * 4.0f * lt * (1.0f - lt);
        // Same soft wall as roaming — the arc never exits the sides.
        d.x = std::max(d.len * 0.55f,
                       std::min(w - d.len * 0.55f, d.x));
        d.leap_t += 1.0f / d.leap_dur;
        if (d.leap_t >= 1.0f) {
            d.leap_t = -1.0f;
            d.y = d.leap_y0;
            // Re-entry: dramatic splash + foam at the head.
            const float hx = d.x + std::cos(d.heading) * 0.4f * d.len;
            splash(hx, static_cast<float>(surface_y_),
                   10 + static_cast<int>(10 * d.depth), d.speed * d.leap_dir);
            foam(hx, 0.9f + 0.5f * d.depth);
        }
    }

    // ---- procedural dolphin rendering ----

    // Pixel plot with tank physics: pixels at/below the shimmer line
    // are underwater (dimmed, refracted by a horizontal wobble);
    // pixels above it are in air (full brightness). When the dolphin
    // heads left the whole render is mirrored around the anchor x, so
    // the dorsal fin always stays on the back.
    void plotD(Framebuffer& fb, float x, float y, int tone) {
        float fx = flip_ ? (2.0f * anchor_x_ - x) : x;
        const int iy = static_cast<int>(y);
        const uint32_t* pal = pal_[0];
        if (iy > surface_y_ + 1) {
            pal = pal_[1];
            fx += 0.9f * u_ * std::sin(y * 0.5f + static_cast<float>(frame_) * 0.22f);
        }
        fb.setPixel(static_cast<int>(fx), iy, pal[tone]);
    }

    // Walk a polyline plotting a tone (fin outlines).
    void linePlot(Framebuffer& fb, float x1, float y1, float x2, float y2, int tone) {
        const float dx = x2 - x1, dy = y2 - y1;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const int steps = std::max(1, static_cast<int>(dist * 2.0f));
        for (int i = 0; i <= steps; ++i) {
            const float f = static_cast<float>(i) / steps;
            plotD(fb, x1 + dx * f, y1 + dy * f, tone);
        }
    }

    // Scanline-free triangle fill via edge functions (fins, flukes).
    void fillTri(Framebuffer& fb, float x1, float y1, float x2, float y2,
                 float x3, float y3, int tone) {
        const float minx = std::min({x1, x2, x3}), maxx = std::max({x1, x2, x3});
        const float miny = std::min({y1, y2, y3}), maxy = std::max({y1, y2, y3});
        const float det = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3);
        if (std::fabs(det) < 1e-6f) return;
        for (int y = static_cast<int>(miny); y <= static_cast<int>(maxy + 0.5f); ++y) {
            for (int x = static_cast<int>(minx); x <= static_cast<int>(maxx + 0.5f); ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                const float a = ((y2 - y3) * (px - x3) + (x3 - x2) * (py - y3)) / det;
                const float b = ((y3 - y1) * (px - x3) + (x1 - x3) * (py - y3)) / det;
                const float c = 1.0f - a - b;
                if (a >= 0.0f && b >= 0.0f && c >= 0.0f)
                    plotD(fb, static_cast<float>(x), static_cast<float>(y), tone);
            }
        }
    }

    void drawDolphin(Framebuffer& fb, const Dolphin& d, float pulse) {
        const float L = d.len;
        const float r_max = L * 0.062f;
        // Left-heading dolphins render facing right (hr) and are
        // mirrored per-pixel in plotD. At exactly vertical both forms
        // coincide, so U-turns pass through nose-up/down seamlessly.
        flip_ = std::cos(d.heading) < 0.0f;
        anchor_x_ = d.x;
        const float hr = flip_ ? ((d.heading > 0.0f) ? PI - d.heading
                                                     : -PI - d.heading)
                               : d.heading;
        const float dirx = std::cos(hr), diry = std::sin(hr);
        // Ventral perpendicular (dorsal is its negative).
        const float px = -diry, py = dirx;
        // Leap flex: C-arch, strongest at the apex.
        const float flex = (d.leap_t >= 0.0f)
                           ? L * 0.09f * std::sin(PI * d.leap_t) : 0.0f;
        const float amp = L * 0.045f * (0.7f + 0.6f * energy_);

        // Palette: 4 OEL cyan tones, a faint top-down light gradient,
        // plus dimmed underwater variants (the tank's open water is
        // clearer than the old below-the-line dim).
        const float hue = 190.0f + 12.0f * d.depth;  // 190..202 cyan-blue
        const float grad = 1.0f - 0.30f * (d.y / static_cast<float>(h_));
        const float bright = std::min(1.0f, (0.28f + 0.55f * d.depth
                                             + 0.18f * pulse) * grad);
        pal_[0][OUTLINE] = hsv_to_rgb(hue, 0.90f, bright * 0.20f);
        pal_[0][MID]     = hsv_to_rgb(hue, 0.85f, bright * 0.78f);
        pal_[0][BELLY]   = hsv_to_rgb(hue, 0.80f, bright * 0.60f);
        pal_[0][EDGE]    = hsv_to_rgb(hue, 0.70f, std::min(1.0f, bright * 1.08f));
        pal_[0][FIN]     = hsv_to_rgb(hue, 0.85f, bright * 0.68f);
        pal_[0][SPEC]    = hsv_to_rgb(hue, 0.35f, std::min(1.0f, bright + 0.38f));
        for (int k = 0; k < TONE_COUNT; ++k) {
            const float vs = (k == SPEC) ? 0.55f : 0.72f;
            pal_[1][k] = scaleBrightness(pal_[0][k], vs);
        }

        // Spine: ~0.75 px per sample so the parallel shading lines of
        // adjacent samples always overlap (no pinholes in the fill).
        static constexpr int MAXS = 384;
        const int n = std::max(24, std::min(MAXS, static_cast<int>(L * 1.4f)));
        float cx[MAXS], cy[MAXS], rd[MAXS], rv[MAXS];
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / (n - 1);
            // Traveling-wave undulation: small at head, large at tail.
            const float s = std::max(0.0f, (t - 0.08f) / 0.92f);
            const float lat = amp * s * s * std::sin(6.2832f * 1.1f * t - d.phase)
                              - flex * std::sin(PI * t);  // arch toward dorsal
            cx[i] = d.x + dirx * (t - 0.5f) * L + px * lat;
            cy[i] = d.y + diry * (t - 0.5f) * L + py * lat;
            rd[i] = profileAt(PROF_D, t) * r_max;
            rv[i] = profileAt(PROF_V, t) * r_max;
        }
        const auto idx = [n](float t) {
            return std::max(0, std::min(n - 1, static_cast<int>(t * (n - 1) + 0.5f)));
        };

        // 1) Dark outline halo (drawn first, body covers its inside).
        for (int i = 0; i < n; ++i) {
            plotD(fb, cx[i] - px * (rd[i] + 1.3f), cy[i] - py * (rd[i] + 1.3f), OUTLINE);
            plotD(fb, cx[i] + px * (rv[i] + 1.3f), cy[i] + py * (rv[i] + 1.3f), OUTLINE);
        }
        plotD(fb, cx[0] - dirx * 1.3f, cy[0] - diry * 1.3f, OUTLINE);          // beak cap
        plotD(fb, cx[n - 1] + dirx * 1.3f, cy[n - 1] + diry * 1.3f, OUTLINE);  // notch cap

        // 2) Dorsal fin: swept-back triangle at ~40% along the spine.
        {
            const int i0 = idx(0.36f), i1 = idx(0.50f), it = idx(0.57f);
            const float b0x = cx[i0] - px * rd[i0] * 0.8f, b0y = cy[i0] - py * rd[i0] * 0.8f;
            const float b1x = cx[i1] - px * rd[i1] * 0.8f, b1y = cy[i1] - py * rd[i1] * 0.8f;
            const float tx = cx[it] - px * (rd[it] + r_max * 1.6f) + dirx * L * 0.02f;
            const float ty = cy[it] - py * (rd[it] + r_max * 1.6f) + diry * L * 0.02f;
            linePlot(fb, b0x, b0y, tx, ty, OUTLINE);
            linePlot(fb, tx, ty, b1x, b1y, OUTLINE);
            fillTri(fb, b0x, b0y, b1x, b1y, tx, ty, FIN);
        }

        // 3) Near-side pectoral flipper at ~55%, pointing down-back.
        {
            const int i0 = idx(0.52f), i1 = idx(0.58f);
            const float b0x = cx[i0] + px * rv[i0] * 0.6f, b0y = cy[i0] + py * rv[i0] * 0.6f;
            const float b1x = cx[i1] + px * rv[i1] * 0.6f, b1y = cy[i1] + py * rv[i1] * 0.6f;
            const float tx = cx[i1] + px * (rv[i1] + r_max * 2.0f) + dirx * L * 0.05f;
            const float ty = cy[i1] + py * (rv[i1] + r_max * 2.0f) + diry * L * 0.05f;
            linePlot(fb, b0x, b0y, tx, ty, OUTLINE);
            linePlot(fb, tx, ty, b1x, b1y, OUTLINE);
            fillTri(fb, b0x, b0y, b1x, b1y, tx, ty, FIN);
        }

        // 4) V-spread flukes, flexing hardest with the undulation.
        {
            float fx = cx[n - 1] - cx[n - 5], fy = cy[n - 1] - cy[n - 5];
            const float fl = std::sqrt(fx * fx + fy * fy);
            if (fl > 1e-3f) { fx /= fl; fy /= fl; } else { fx = dirx; fy = diry; }
            const float flap = std::sin(6.2832f * 1.1f - d.phase) * r_max * 1.3f;
            const float fluke_len = r_max * 2.6f;
            const float nx = cx[n - 1], ny = cy[n - 1];
            for (int k = -1; k <= 1; k += 2) {
                const float tipx = nx + fx * fluke_len * 0.75f
                                   + (-fy) * (k * fluke_len * 0.6f + flap);
                const float tipy = ny + fy * fluke_len * 0.75f
                                   + (fx) * (k * fluke_len * 0.6f + flap);
                const float w0x = nx + (-fy) * r_max * 0.25f * k;
                const float w0y = ny + (fx) * r_max * 0.25f * k;
                const float w1x = nx - fx * r_max * 0.5f;
                const float w1y = ny - fy * r_max * 0.5f;
                linePlot(fb, w0x, w0y, tipx, tipy, OUTLINE);
                linePlot(fb, tipx, tipy, w1x, w1y, OUTLINE);
                fillTri(fb, w0x, w0y, w1x, w1y, tipx, tipy, FIN);
            }
        }

        // 5) Body fill: per-sample lines along the local normal, shaded
        // by distance from the dorsal edge (top-lit).
        for (int i = 0; i < n; ++i) {
            const float total = rd[i] + rv[i];
            if (total < 0.5f) {
                plotD(fb, cx[i], cy[i], MID);
                continue;
            }
            for (float s2 = -rd[i]; s2 <= rv[i]; s2 += 0.6f) {
                const float frac = (s2 + rd[i]) / total;
                const int tone = (frac < 0.20f) ? EDGE : (frac > 0.82f) ? BELLY : MID;
                plotD(fb, cx[i] + px * s2, cy[i] + py * s2, tone);
            }
            plotD(fb, cx[i] + px * rv[i], cy[i] + py * rv[i], MID);
        }

        // 6) Speculars: brightest dots at the melon and the eye.
        {
            const int im = idx(0.14f);
            const float mx = cx[im] - px * rd[im] * 0.45f;
            const float my = cy[im] - py * rd[im] * 0.45f;
            plotD(fb, mx, my, SPEC);
            plotD(fb, mx + 1.0f, my, SPEC);
            const int ie = idx(0.22f);
            plotD(fb, cx[ie] + px * rv[ie] * 0.15f, cy[ie] + py * rv[ie] * 0.15f, SPEC);
        }
    }

    static uint32_t scaleBrightness(uint32_t c, float f) {
        const uint8_t r = static_cast<uint8_t>(((c >> 16) & 0xFF) * f);
        const uint8_t g = static_cast<uint8_t>(((c >> 8) & 0xFF) * f);
        const uint8_t b = static_cast<uint8_t>((c & 0xFF) * f);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

    // ---- water surface (top), plankton, foam, droplets ----

    void drawWater(Framebuffer& fb, float pulse, int w) {
        const float f = static_cast<float>(frame_);
        // Shimmer line across the top: two sine noises give a slow
        // wander + chop; beats brighten it.
        for (int x = 0; x < w; ++x) {
            const float nse = std::sin(x * 0.045f + f * 0.09f)
                              + 0.7f * std::sin(x * 0.11f - f * 0.13f);
            const int wy = surface_y_ + static_cast<int>(std::lround(nse * 1.1f * u_));
            const float shimmer = 0.5f + 0.5f * std::sin(x * 0.11f + f * 0.16f + nse);
            const float v = 0.13f + 0.11f * shimmer + 0.20f * pulse;
            fb.setPixel(x, wy, hsv_to_rgb(197.0f, 0.9f, std::min(1.0f, v)));
            fb.setPixel(x, wy + 1, hsv_to_rgb(197.0f, 0.9f, v * 0.45f));
        }
        // Plankton motes: sparse dots drifting slowly with the current,
        // depth-dimmed like everything else.
        for (const Mote& m : motes_) {
            float mx = std::fmod(m.bx * w + f * m.spd * 8.0f, static_cast<float>(w));
            if (mx < 0.0f) mx += w;
            const float my = m.by * h_ + std::sin(f * 0.015f + m.ph) * 3.0f * u_;
            const float tw = 0.5f + 0.5f * std::sin(f * 0.05f + m.ph * 3.0f);
            const float v = (0.04f + 0.07f * tw) * (1.0f - 0.4f * m.by);
            fb.setPixel(static_cast<int>(mx), static_cast<int>(my),
                        hsv_to_rgb(197.0f, 0.85f, v));
        }
    }

    void splash(float x, float y, int count, float base_vx) {
        for (int i = 0; i < count; ++i) {
            Particle p;
            p.x = x + (rnd() - 0.5f) * 10.0f;
            p.y = y;
            p.vx = base_vx * 0.4f + (rnd() - 0.5f) * 2.6f;
            p.vy = -(0.5f + rnd() * 2.6f);
            p.max_life = p.life = 16.0f + rnd() * 16.0f;
            particles_.push_back(p);
        }
    }

    void foam(float x, float strength) {
        if (foams_.size() >= 24) return;
        Foam f;
        f.x = x;
        f.max_life = f.life = 20.0f + 14.0f * strength;
        f.spread = 2.0f;
        f.grow = 0.8f + 0.6f * strength;
        foams_.push_back(f);
    }

    void updateFoam(Framebuffer& fb) {
        for (auto it = foams_.begin(); it != foams_.end();) {
            Foam& f = *it;
            f.spread += f.grow * u_;
            f.life -= 1.0f;
            if (f.life <= 0.0f) {
                it = foams_.erase(it);
                continue;
            }
            const float a = f.life / f.max_life;
            for (float dx = -f.spread; dx <= f.spread; dx += 1.0f) {
                const float fall = 1.0f - std::fabs(dx) / f.spread;
                // Deterministic hash jitter so the foam reads granular.
                const float hh = std::fmod(std::fabs(
                    std::sin((f.x + dx) * 12.9898f + f.spread * 3.7f) * 43758.5453f), 1.0f);
                if (hh > a * fall + 0.15f) continue;
                const int yy = surface_y_ + ((hh < 0.3f) ? 1 : 0);
                const float v = 0.10f + 0.45f * a * fall;
                fb.setPixel(static_cast<int>(f.x + dx), yy,
                            hsv_to_rgb(195.0f, 0.4f, std::min(1.0f, v)));
            }
            ++it;
        }
    }

    void updateParticles(Framebuffer& fb) {
        for (auto it = particles_.begin(); it != particles_.end();) {
            Particle& p = *it;
            p.x += p.vx * u_;
            p.y += p.vy * u_;
            p.vy += 0.14f;  // gravity
            p.life -= 1.0f;
            if (p.life <= 0.0f || p.y > fb.height()) {
                it = particles_.erase(it);
                continue;
            }
            // Droplets that fall back below the shimmer line fade fast.
            float fade = p.life / p.max_life;
            if (p.y > surface_y_ + 2) fade *= 0.4f;
            fb.setPixel(static_cast<int>(p.x), static_cast<int>(p.y),
                        hsv_to_rgb(192.0f, 0.55f, std::min(1.0f, 0.25f + 0.75f * fade)));
            ++it;
        }
    }

    uint32_t seed_ = 0xD01F1234u;
    long frame_ = 0;
    long last_beat_frame_ = -1;
    float u_ = 1.0f;
    float energy_ = 0.0f;
    int h_ = 60;
    int surface_y_ = 4;
    bool flip_ = false;
    float anchor_x_ = 0.0f;
    uint32_t pal_[2][TONE_COUNT] = {};
    std::vector<Dolphin> dolphins_;
    std::vector<Particle> particles_;
    std::vector<Foam> foams_;
    std::vector<Mote> motes_;
};

REGISTER_PRESET("dolphins", DolphinsPreset);
