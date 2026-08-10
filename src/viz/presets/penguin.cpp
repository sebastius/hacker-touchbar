#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "viz/pioneer_hues.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/color.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "viz/presets/penguin_sprites.h"

// "penguin" — the Gameboy penguin from the video, cut out of its
// scene by tools/penguin_extract.py (flood-fill background removal +
// tone classification, despeckle + temporal-median anti-flicker,
// baked into penguin_sprites.h as 3-bit RLE sprites: 32 dance poses
// on a feet-anchored fixed canvas). Sprites are nearest-neighbor
// upscaled at runtime to nearly the full bar height (1.5x on the
// 60 px bar — a clean pixel-art ratio).
//
// A flock loiters on the bar: idle swaying and super-slow strolls
// when nothing is playing — often strolling over to a neighbor,
// because loitering is social: when two idle penguins meet they
// high-five (a synchronized hop with a spark) or hearts pop up
// between them and drift away. With music they go feral — dancing in
// place, sprinting across the bar, belly slides every now and then —
// and 808s launch the whole flock: a bass-band onset detector (low
// FFT bins vs their short-term average) fires the jumps, hit strength
// sets the height, and the biggest hits occasionally punt someone
// into a slide. Ordinary beats only earn a small hop. Silence settles
// them back to the lazy sway. Behind them runs a spectrum ridgeline
// in the main hue — bright crest, fading into the dark so the
// sprites pop on top. Penguins bounce off each other: runners knock
// bystanders into a stumble, sliders rebound, and on a lively track
// a collision can pop both into the air. Body and outline tones
// follow pioneerHues().main, the mid-tone back/flippers follow
// pioneerHues().accent, so the colors mode's two sliders restyle
// the flock. No feedback trails (cleared
// every frame — smear ruins the sprites).
class PenguinPreset : public VizPreset {
public:
    const char* name() const override { return "penguin"; }

    // Clears every frame — no trail pass needed.
    bool clearsFrame() const override { return true; }

    // Penguins currently in the air (test hook).
    int airborneCount() const override {
        int n = 0;
        for (const Penguin& p : penguins_)
            if (p.y_off < -2.0f) ++n;
        return n;
    }

    PenguinPreset() {
        for (int i = 0; i < 10; ++i) {
            Penguin p;
            p.phase = rnd() * 6.2832f;
            p.anim = static_cast<int>(rnd() * PENGUIN_FRAME_COUNT);
            penguins_.push_back(p);
        }
    }

    void draw(const VizRenderer& viz, Framebuffer& fb) override {
        const int w = fb.width();
        const int h = fb.height();
        ground_ = h - 2;
        fb.clear(0xFF000000);
        ensureDecoded(h - 12);  // headroom for hops on the 60 px bar

        ++frame_;
        energy_ = audioEnergy(viz);
        const float bass = lowBand(viz);
        const float pulse = viz.pulse();
        const bool silent = viz.silent();
        // pulse resets to 1.0 on a beat, so this fires once per beat.
        const bool fresh_beat = pulse > 0.95f && frame_ != last_beat_frame_;
        if (fresh_beat) last_beat_frame_ = frame_;

        // 808 detection: the low band spiking above its short-term
        // average is a bass hit; strength scales the flock's jumps.
        bass_hist_[bass_pos_ % kBassHist] = bass;
        ++bass_pos_;
        float bass_avg = 0.0f;
        const int hist_n = static_cast<int>(std::min<long>(bass_pos_, kBassHist));
        for (int i = 0; i < hist_n; ++i) bass_avg += bass_hist_[i];
        bass_avg /= std::max(1, hist_n);
        bool bass_hit = false;
        float hit_strength = 0.0f;
        if (!silent && bass_pos_ > 16 && bass > 0.28f
            && bass > bass_avg * 1.45f && frame_ - last_808_ > 10) {
            bass_hit = true;
            last_808_ = frame_;
            hit_strength = std::min(1.0f, (bass - bass_avg) * 2.2f);
        }

        // Body (brightest tone, dominant) and outline from the main
        // hue, mid-tone back/flippers from the accent hue — the
        // colors mode's two sliders restyle the flock.
        const float main_hue = pioneerHues().main;
        const float accent_hue = pioneerHues().accent;
        const float boost = 1.0f + 0.25f * pulse;
        tone_[1] = hue_rgb(main_hue, 0.85f, std::min(1.0f, 0.50f * boost));
        tone_[2] = hue_rgb(accent_hue, 0.75f, std::min(1.0f, 0.80f * boost));
        tone_[3] = hue_rgb(main_hue, 0.85f, std::min(1.0f, 1.00f * boost));
        // Beak/feet stay warm yellow and the headband red — the
        // character's brand colors, only brightened by the beat.
        tone_[4] = hsv_to_rgb(45.0f, 0.95f, std::min(1.0f, 0.95f * boost));
        tone_[5] = hsv_to_rgb(4.0f, 0.85f, std::min(1.0f, 0.85f * boost));

        // Backdrop: a spectrum ridgeline in the main hue behind the
        // flock — bright crest line, fading downward into the dark so
        // the penguins pop on top. Silent = a faint flat horizon glow.
        const auto& bars = viz.bars();
        const int nb = static_cast<int>(bars.size());
        const int floor_y = ground_ - 4;  // fade ends above the ground line
        if (nb > 0) {
            const uint32_t crest =
                hue_rgb(main_hue, 0.85f, std::min(1.0f, 0.50f * boost));
            for (int x = 0; x < w; ++x) {
                const float v = std::sqrt(std::max(0.0f, bars[x * nb / w]));
                const int top = std::max(
                    2, floor_y - 2 - static_cast<int>(v * (floor_y - 8)));
                fb.setPixel(x, top, crest);
                const float span = static_cast<float>(floor_y - top + 1);
                for (int y = top + 1; y <= floor_y; ++y) {
                    const float fade = 1.0f - (y - top) / span;
                    fb.setPixel(x, y, hue_rgb(main_hue, 0.85f,
                                std::min(1.0f, 0.34f * fade * boost)));
                }
            }
        }

        // Faint ground line.
        const uint32_t gnd = hue_rgb(main_hue, 0.85f, 0.16f);
        for (int x = 0; x < w; ++x) fb.setPixel(x, ground_ + 1, gnd);

        for (size_t i = 0; i < penguins_.size(); ++i) {
            Penguin& p = penguins_[i];
            if (!p.init) {
                p.init = true;
                const float fi = static_cast<float>(i)
                                 / static_cast<float>(penguins_.size() - 1);
                p.x = w * (0.10f + 0.80f * fi) + (rnd() - 0.5f) * w * 0.06f;
                p.dir = (rnd() < 0.5f) ? 1.0f : -1.0f;
            }
            update(p, w, silent, fresh_beat, bass, bass_hit, hit_strength);
        }
        if (silent) maybeSocial();
        resolveCollisions(silent);
        for (const Penguin& p : penguins_) drawPenguin(fb, p);

        // Interaction particles (hearts, high-five sparks) rise and
        // fade above the flock.
        for (auto it = particles_.begin(); it != particles_.end();) {
            Particle& q = *it;
            if (q.delay > 0) { --q.delay; ++it; continue; }
            ++q.t;
            q.y += q.vy;
            if (q.t > q.life) { it = particles_.erase(it); continue; }
            drawParticle(fb, q);
            ++it;
        }
    }

private:
    enum State { IDLE, WADDLE, DANCE, RUN, JUMP, SLIDE, GREET };

    struct Penguin {
        bool init = false;
        float x = 0.0f;           // center x, px
        float y_off = 0.0f;       // height above ground, px (jumps)
        float vy = 0.0f;
        float dir = 1.0f;         // facing: +1 right, -1 left
        float phase = 0.0f;       // idle bob phase
        float target_x = 0.0f;    // waddle/run destination
        int state = IDLE;
        int state_t = 0;          // frames in state
        int slide_len = 26;       // SLIDE duration, randomized per slide
        int anim = 0;             // sprite index
        long last_bounce = -100;  // hop cooldown after a collision
        int greet_len = 0;        // GREET duration
        int greet_hop = -1;       // frame within GREET to hop (-1: no hop)
        long last_social = -1000; // interaction cooldown
    };

    struct Particle {
        float x, y;
        float vy;
        int delay;   // frames until it appears
        int t;
        int life;
        int kind;    // 0 = heart, 1 = spark
    };

    struct Scaled {
        int w = 0, h = 0;
        std::vector<uint8_t> px;
    };

    // Deterministic LCG so dumps/tests are reproducible.
    float rnd() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<float>((seed_ >> 8) & 0xFFFFFF) / 16777216.0f;
    }

    float halfW() const { return scaled_w_ * 0.5f; }

    static float audioEnergy(const VizRenderer& viz) {
        if (viz.silent()) return 0.0f;
        const auto& bars = viz.bars();
        float sum = 0.0f;
        for (float v : bars) sum += std::sqrt(std::max(0.0f, v));
        float e = sum / std::max<size_t>(1, bars.size());
        return std::min(1.0f, e * 1.4f);
    }

    static float lowBand(const VizRenderer& viz) {
        if (viz.silent()) return 0.0f;
        const auto& bars = viz.bars();
        const size_t k = std::max<size_t>(1, bars.size() / 12);
        float sum = 0.0f;
        for (size_t i = 0; i < k; ++i) sum += std::sqrt(std::max(0.0f, bars[i]));
        return std::min(1.0f, sum / k * 2.0f);
    }

    void update(Penguin& p, int w, bool silent, bool fresh_beat,
                float bass, bool bass_hit, float hit_strength) {
        ++p.state_t;

        // 808s launch the flock: nearly everyone jumps, height set by
        // the hit's strength, and the biggest hits sometimes punt a
        // penguin straight into a belly slide. Ordinary beats only
        // earn a small hop from a few. Greeting pairs finish their
        // interaction first.
        if (p.state != JUMP && p.state != SLIDE && p.state != GREET) {
            if (bass_hit && rnd() < 0.92f) {
                if (hit_strength > 0.65f && rnd() < 0.12f) {
                    p.state = SLIDE;
                    p.state_t = 0;
                    p.slide_len = 26 + static_cast<int>(14 * rnd());
                    p.dir = (rnd() < 0.5f) ? 1.0f : -1.0f;
                } else {
                    p.state = JUMP;
                    p.state_t = 0;
                    // apex ~5-11 px: the sprite is nearly bar-tall
                    p.vy = -(2.1f + 0.7f * hit_strength + 0.3f * rnd());
                }
            } else if (fresh_beat && rnd() < 0.30f) {
                p.state = JUMP;
                p.state_t = 0;
                p.vy = -(1.6f + 0.5f * rnd());  // polite little hop
            } else if (p.state != JUMP && !silent
                       && ((bass > 0.45f && rnd() < 0.020f)
                           || (energy_ > 0.45f && rnd() < 0.004f))) {
                p.state = SLIDE;
                p.state_t = 0;
                p.slide_len = 26 + static_cast<int>(14 * rnd());
                p.dir = (rnd() < 0.5f) ? 1.0f : -1.0f;
            }
        }

        const float margin = halfW() + 2.0f;
        switch (p.state) {
        case IDLE:
            p.anim += (frame_ % 6 == 0) ? 1 : 0;   // lazy sway
            if (!silent && energy_ > 0.15f) {
                p.state = DANCE; p.state_t = 0;
            } else if (p.state_t > 60 + 120 * rnd()) {
                // Mosey to a new spot — silent wandering is a slow,
                // unhurried stroll, and more often than not it's a
                // visit: loitering is social, so amble over to the
                // nearest neighbor and see what happens.
                p.state = WADDLE; p.state_t = 0;
                float near_x = -1.0f, best = 1e9f;
                for (const Penguin& q : penguins_) {
                    if (&q == &p) continue;
                    const float d = std::fabs(q.x - p.x);
                    if (d < best) { best = d; near_x = q.x; }
                }
                if (silent && near_x >= 0.0f && rnd() < 0.55f) {
                    const float off = scaled_w_ * (1.0f + 0.4f * rnd());
                    p.target_x = near_x + (p.x < near_x ? -off : off);
                } else {
                    p.target_x = w * (0.06f + 0.88f * rnd());
                }
                p.target_x = std::max(margin, std::min(w - margin, p.target_x));
                p.dir = (p.target_x > p.x) ? 1.0f : -1.0f;
            }
            break;
        case WADDLE:
            p.anim += (frame_ % 3 == 0) ? 1 : 0;
            p.x += p.dir * (silent ? 0.22f : (0.45f + 0.5f * energy_));
            if (std::fabs(p.x - p.target_x) < 2.0f || p.state_t > 900) {
                p.state = silent ? IDLE : DANCE; p.state_t = 0;
            }
            break;
        case DANCE:
            p.anim += (frame_ % 2 == 0) ? 1 : 0;   // full-speed groove
            if (silent && p.state_t > 30) {
                p.state = IDLE; p.state_t = 0;
            } else if (p.state_t > 90 + 180 * rnd()) {
                if (rnd() < 0.65f) {
                    p.state = RUN; p.state_t = 0;
                    p.target_x = w * (0.06f + 0.88f * rnd());
                    p.dir = (p.target_x > p.x) ? 1.0f : -1.0f;
                } else {
                    p.state = IDLE; p.state_t = 0;
                }
            }
            break;
        case RUN:
            p.anim += (frame_ % 2 == 0) ? 1 : 0;
            p.x += p.dir * (3.2f + 3.0f * energy_);
            if (std::fabs(p.x - p.target_x) < 4.0f || p.state_t > 300) {
                p.state = DANCE; p.state_t = 0;
            }
            break;
        case JUMP:
            p.anim += (frame_ % 3 == 0) ? 1 : 0;
            p.y_off += p.vy;
            p.vy += 0.42f;  // gravity
            if (p.y_off >= 0.0f) {
                p.y_off = 0.0f;
                p.state = silent ? IDLE : DANCE;
                p.state_t = 0;
            }
            break;
        case SLIDE:
            p.x += p.dir * (3.0f + 2.0f * energy_);
            if (p.state_t > p.slide_len || p.x < margin || p.x > w - margin) {
                p.state = DANCE; p.state_t = 0;
                p.dir = -p.dir;  // face back the way we came
            }
            break;
        case GREET:
            p.anim += (frame_ % 4 == 0) ? 1 : 0;   // excited jig
            if (p.greet_hop >= 0 && p.state_t == p.greet_hop)
                p.vy = -1.7f;  // the high-five hop
            if (p.y_off < 0.0f || p.vy < 0.0f) {
                p.y_off += p.vy;
                p.vy += 0.42f;
                if (p.y_off >= 0.0f) { p.y_off = 0.0f; p.vy = 0.0f; }
            }
            if ((!silent && p.state_t > 10) || p.state_t > p.greet_len) {
                p.state = silent ? IDLE : DANCE;
                p.state_t = 0;
                p.greet_hop = -1;
            }
            break;
        }

        p.x = std::max(margin, std::min(w - margin, p.x));
        p.anim %= PENGUIN_FRAME_COUNT;
    }

    // Idle interactions: when the nearest neighbors have drifted
    // within reach and everyone's cooled down, pick a pair and play
    // out a little moment — a high five (synchronized hop + spark)
    // or hearts popping up between them.
    void maybeSocial() {
        if (frame_ < next_social_) return;
        const float near = scaled_w_ * 1.4f;
        for (size_t i = 0; i < penguins_.size(); ++i) {
            Penguin& a = penguins_[i];
            if (a.state != IDLE || frame_ - a.last_social < 400) continue;
            for (size_t j = i + 1; j < penguins_.size(); ++j) {
                Penguin& b = penguins_[j];
                if (b.state != IDLE || frame_ - b.last_social < 400) continue;
                const float dx = b.x - a.x;
                if (std::fabs(dx) > near || std::fabs(dx) < halfW()) continue;
                a.state = b.state = GREET;
                a.state_t = b.state_t = 0;
                a.greet_len = b.greet_len = 55;
                a.dir = dx > 0.0f ? 1.0f : -1.0f;  // face each other
                b.dir = -a.dir;
                a.last_social = b.last_social = frame_;
                const float mid = (a.x + b.x) * 0.5f;
                if (rnd() < 0.5f) {
                    // High five: simultaneous hop, spark at contact.
                    a.greet_hop = b.greet_hop = 12;
                    particles_.push_back(
                        {mid, static_cast<float>(ground_ - scaled_h_ + 4),
                         0.0f, 18, 0, 16, 1});
                } else {
                    // Hearts popping up between them.
                    for (int k = 0; k < 3; ++k)
                        particles_.push_back(
                            {mid + (rnd() - 0.5f) * 8.0f,
                             static_cast<float>(ground_ - scaled_h_ + 6),
                             -(0.35f + 0.15f * rnd()), 6 + k * 12, 0, 55, 0});
                }
                next_social_ = frame_ + 260 + static_cast<long>(300 * rnd());
                return;
            }
        }
        next_social_ = frame_ + 90;  // nobody close — check again soon
    }

    void drawParticle(Framebuffer& fb, const Particle& q) {
        const float fade = 1.0f - static_cast<float>(q.t) / q.life;
        if (q.kind == 0) {
            // 5x5 heart in the accent hue, dimming as it rises.
            static const char* rows[5] = {
                "XX.XX", "XXXXX", "XXXXX", ".XXX.", "..X.."
            };
            const uint32_t c =
                hue_rgb(pioneerHues().accent, 0.90f, 0.95f * fade);
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 5; ++x)
                    if (rows[y][x] == 'X')
                        fb.setPixel(static_cast<int>(q.x) - 2 + x,
                                    static_cast<int>(q.y) + y, c);
        } else {
            // 3x3 spark plus in warm yellow — the high-five contact.
            static const char* rows[3] = {".X.", "XXX", ".X."};
            const uint32_t c = hsv_to_rgb(45.0f, 0.90f, fade);
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 3; ++x)
                    if (rows[y][x] == 'X')
                        fb.setPixel(static_cast<int>(q.x) - 1 + x,
                                    static_cast<int>(q.y) + y, c);
        }
    }

    // Penguins shove and bounce off each other. Airborne penguins
    // (mid-hop) sail over grounded ones; grounded pairs separate,
    // whoever was moving into the other gets turned around, and
    // bystanders hit by a runner or slider stumble away. On a lively
    // track a collision sometimes pops both into the air. Greeting
    // pairs are left alone — don't break up the moment.
    void resolveCollisions(bool silent) {
        const float hw = halfW() * 0.75f;  // forgiving hitbox
        const size_t n = penguins_.size();
        for (size_t i = 0; i < n; ++i) {
            for (size_t j = i + 1; j < n; ++j) {
                Penguin& a = penguins_[i];
                Penguin& b = penguins_[j];
                if (a.y_off < -4.0f || b.y_off < -4.0f) continue;
                const float dx = b.x - a.x;
                const float overlap = 2.0f * hw - std::fabs(dx);
                if (overlap <= 0.0f) continue;
                const float s = (dx >= 0.0f) ? 1.0f : -1.0f;
                a.x -= s * overlap * 0.5f;
                b.x += s * overlap * 0.5f;
                const bool moving = (a.state == WADDLE || a.state == RUN
                                     || a.state == SLIDE)
                                 || (b.state == WADDLE || b.state == RUN
                                     || b.state == SLIDE);
                if (!moving) continue;  // just standing close
                bounceOff(a, -s, silent);
                bounceOff(b, s, silent);
            }
        }
    }

    void bounceOff(Penguin& p, float away_dir, bool silent) {
        if (p.state == GREET) return;  // don't break up the moment
        p.dir = away_dir;
        if (p.state == WADDLE || p.state == RUN) {
            p.target_x = p.x + away_dir * (60.0f + 120.0f * rnd());
        } else if (p.state == IDLE || p.state == DANCE) {
            // knocked by a runner/slider: stumble away
            p.state = WADDLE; p.state_t = 0;
            p.target_x = p.x + away_dir * (50.0f + 80.0f * rnd());
        }
        // SLIDE keeps sliding, just reversed by the dir flip.
        if (!silent && energy_ > 0.25f && frame_ - p.last_bounce > 25
            && p.state != JUMP && rnd() < 0.30f) {
            p.state = JUMP; p.state_t = 0;
            p.vy = -(2.0f + 0.8f * rnd());
        }
        p.last_bounce = frame_;
    }

    void ensureDecoded(int target_h) {
        if (!frames_.empty() && target_h == scaled_h_) return;
        frames_.clear();
        scaled_h_ = target_h;
        for (int i = 0; i < PENGUIN_FRAME_COUNT; ++i)
            frames_.push_back(decodeScaled(PENGUIN_FRAMES[i], target_h));
        slide_ = decodeScaled(PENGUIN_SLIDE, target_h);
        scaled_w_ = frames_[0].w;
    }

    static Scaled decodeScaled(const PenguinSprite& s, int target_h) {
        std::vector<uint8_t> raw;
        raw.reserve(static_cast<size_t>(s.w) * s.h);
        for (int i = 0; i < s.rle_len; ++i)
            raw.insert(raw.end(), s.rle[i] >> 3, s.rle[i] & 7);
        // Nearest-neighbor upscale to the target height (1.5x on the
        // 60 px bar — a clean pixel-art ratio).
        Scaled out;
        out.h = target_h;
        const float scale = static_cast<float>(target_h) / s.h;
        out.w = std::max(1, static_cast<int>(s.w * scale + 0.5f));
        out.px.resize(static_cast<size_t>(out.w) * out.h);
        for (int y = 0; y < out.h; ++y) {
            const int sy = std::min(s.h - 1, static_cast<int>(y / scale));
            for (int x = 0; x < out.w; ++x) {
                const int sx = std::min(s.w - 1, static_cast<int>(x / scale));
                out.px[static_cast<size_t>(y) * out.w + x] =
                    raw[static_cast<size_t>(sy) * s.w + sx];
            }
        }
        return out;
    }

    void drawPenguin(Framebuffer& fb, const Penguin& p) {
        // Belly slide: the hardest-leaning dance pose, frozen — at
        // slide speed it reads as a tobogganing lunge. (A squashed or
        // 90-degree-rotated sprite reads as a blob; tried both.)
        const bool sliding = (p.state == SLIDE);
        const Scaled& s = sliding ? slide_ : frames_[p.anim];
        const bool flip = p.dir < 0.0f;
        // Feet on the ground; y_off rides negative while airborne.
        const int base_y = ground_ - s.h + 1 + static_cast<int>(p.y_off);
        const int left = static_cast<int>(p.x) - s.w / 2;
        for (int y = 0; y < s.h; ++y) {
            for (int x = 0; x < s.w; ++x) {
                const int sx = flip ? (s.w - 1 - x) : x;
                const uint8_t t = s.px[static_cast<size_t>(y) * s.w + sx];
                if (t) fb.setPixel(left + x, base_y + y, tone_[t]);
            }
        }
    }

    static constexpr int kBassHist = 32;  // ~0.5 s of low-band history

    uint32_t seed_ = 0xBED9C4E3u;
    long frame_ = 0;
    long last_beat_frame_ = -1;
    float energy_ = 0.0f;
    int ground_ = 58;
    int scaled_w_ = 0;
    int scaled_h_ = 0;
    uint32_t tone_[8] = {};
    std::vector<Penguin> penguins_;
    std::vector<Scaled> frames_;
    Scaled slide_;

    // 808 onset detection
    float bass_hist_[kBassHist] = {};
    long bass_pos_ = 0;
    long last_808_ = -100;

    // idle socials
    long next_social_ = 200;
    std::vector<Particle> particles_;
};

REGISTER_PRESET("penguin", PenguinPreset);
