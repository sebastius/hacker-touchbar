#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Dominant-color analysis of a small RGB image: a hue-bin histogram
// (24 x 15 deg) weighted by saturation AND lightness — the image's
// two brightest color masses become the bar's primary and accent.
// Bins too close to the primary don't count as a second color. When
// no second hue mass exists, the accent comes from the cover too:
// a significant bright-neutral mass (white line art, text) yields
// white, otherwise an analogous +30° shift of the primary — never an
// invented complement. A near-grayscale image yields 396 ("past
// 360"), the theme's white/grayscale mode.
struct ArtAnalysis {
    int w = 0, h = 0;
    std::vector<uint32_t> px;  // ARGB, row-major, w*h
    float main_hue = 396.0f;
    float accent_hue = 396.0f;
};

inline float rgb_to_hue(float r, float g, float b) {
    const float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (mx == mn) return 0.0f;
    const float d = mx - mn;
    float h;
    if (mx == r) h = std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = (b - r) / d + 2.0f;
    else h = (r - g) / d + 4.0f;
    h *= 60.0f;
    return h < 0.0f ? h + 360.0f : h;
}

inline ArtAnalysis analyze_art(const uint8_t* rgb, int w, int h) {
    constexpr int BINS = 24;
    ArtAnalysis out;
    out.w = w;
    out.h = h;
    const size_t npix = static_cast<size_t>(w) * h;
    out.px.resize(npix);

    double wgt[BINS] = {};
    double bcos[BINS] = {};
    double bsin[BINS] = {};
    int voters = 0;
    int neutrals = 0;  // bright, unsaturated pixels (white art/text)

    for (size_t i = 0; i < npix; ++i) {
        const uint8_t r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        out.px[i] = 0xFF000000u | (r << 16) | (g << 8) | b;

        const float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
        const float mx = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
        const float mn = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
        const float l = (mx + mn) / 2.0f;
        const float denom = 1.0f - std::fabs(2.0f * l - 1.0f);
        const float s = denom > 0.0f ? (mx - mn) / denom : 0.0f;
        // Bright neutrals (white line art, text) don't get a hue vote
        // but do count as the cover's second color.
        if (s < 0.20f && l > 0.5f) { ++neutrals; continue; }
        if (l < 0.10f || l > 0.85f) continue;  // true black / glare
        if (s < 0.20f) continue;               // grays don't vote
        // Bright + saturated wins, but dark colors still count (a
        // forest-green block is as much a cover color as a neon one)
        // — just damped, so murk never outvotes real color.
        const double weight = s * std::max(l, 0.35f);
        const float hue = rgb_to_hue(rf, gf, bf);
        const double rad = hue * M_PI / 180.0;
        const int bin = static_cast<int>(hue / 15.0f) % BINS;
        wgt[bin] += weight;
        bcos[bin] += std::cos(rad) * weight;
        bsin[bin] += std::sin(rad) * weight;
        ++voters;
    }

    auto hueOf = [&](int b) {
        double deg = std::atan2(bsin[b], bcos[b]) * 180.0 / M_PI;
        if (deg < 0.0) deg += 360.0;
        return static_cast<float>(deg);
    };

    // Black & white art: almost nobody voted (any "color" in a
    // grayscale image is JPEG chroma noise, which skews blue) — go
    // monochrome.
    if (voters < static_cast<int>(npix) * 0.03) {
        out.main_hue = 396.0f;
        out.accent_hue = 396.0f;
        return out;
    }

    int b0 = -1;
    for (int b = 0; b < BINS; ++b)
        if (wgt[b] > 0.0 && (b0 < 0 || wgt[b] > wgt[b0])) b0 = b;
    if (b0 < 0) return out;

    out.main_hue = hueOf(b0);
    // Runner-up at least 60 deg away, not a whisper of the first.
    int b1 = -1;
    for (int b = 0; b < BINS; ++b) {
        const int dist = std::min((b - b0 + BINS) % BINS, (b0 - b + BINS) % BINS);
        if (dist >= 4 && wgt[b] > wgt[b0] * 0.15 &&
            (b1 < 0 || wgt[b] > wgt[b1]))
            b1 = b;
    }
    if (b1 >= 0) {
        out.accent_hue = hueOf(b1);
    } else if (neutrals > static_cast<int>(npix) * 0.10) {
        // One hue + real white art on the cover: white is the accent.
        out.accent_hue = 396.0f;
    } else {
        // Strictly monochrome: stay on the cover with an analogous
        // shift — never invent a complement that isn't in the art.
        out.accent_hue = std::fmod(out.main_hue + 30.0f, 360.0f);
    }
    return out;
}
