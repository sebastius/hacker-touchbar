#pragma once

#include "gfx/color.hpp"

// Live-adjustable pioneer color scheme, shared between the pioneer
// preset (reads it every frame) and the color sources that write it:
// the MPRIS album-art sync (on each track change) and the user's own
// picks. In-memory only — back to these defaults on daemon restart.
struct PioneerHues {
    float main = 190.0f;   // stock phosphor cyan
    float accent = 12.0f;  // amber complement (focus window)
};

inline PioneerHues& pioneerHues() {
    static PioneerHues hues;
    return hues;
}

// The user's own colors, set by the colors mode sliders or the "hue"
// command. Album-art hues overwrite pioneerHues() per track but never
// this — tapping the album cover on the bar toggles back to it.
inline PioneerHues& manualHues() {
    static PioneerHues hues;
    return hues;
}

// The current track's art-derived colors, written by the art fetch
// callback alongside the live scheme. The cover tap toggles the live
// scheme between this and manualHues().
inline PioneerHues& artHues() {
    static PioneerHues hues;
    return hues;
}

// Hue values past 360 are the colors mode's "white" pick — the top of
// the slider range is reserved for it, meaning zero saturation. Every
// consumer should turn a hue into a color through hue_rgb.
inline bool hue_is_white(float h) { return h > 360.0f; }

inline uint32_t hue_rgb(float h, float s, float v) {
    return hsv_to_rgb(h, hue_is_white(h) ? 0.0f : s, v);
}
