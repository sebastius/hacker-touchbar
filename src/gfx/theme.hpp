#pragma once
#include <cstdint>
#include <string>

class Framebuffer;

// A named widget palette. Widgets read the ACTIVE palette through the
// theme:: namespace variables below (runtime-mutable, so a palette
// swap needs no widget changes); Theme::fromName picks a palette and
// theme::apply installs it.
//
//   "default" — Matrix / Omarchy Aether neon green
//   "green"   — monochrome terminal, green phosphor
//   "amber"   — monochrome terminal, amber phosphor
//   "oel"     — 2000s Pioneer head-unit OEL: monochrome cyan-on-black,
//               1px display-window frames around widget zones
struct Theme {
    // Core colors (slot names are historical — in "oel" the "green"
    // slots hold the cyan family and "red"/"yellow" join the
    // monochrome scale)
    uint32_t bg;
    uint32_t green;        // primary accent
    uint32_t green_dim;    // dimmer accent
    uint32_t green_dark;   // dark accent (outlines)
    uint32_t green_bg;     // very subtle tinted black for panels
    uint32_t panel;        // slightly lighter panel bg
    uint32_t btn;          // button background
    uint32_t btn_border;   // button border
    uint32_t text;         // primary text
    uint32_t text_dim;     // secondary/dim text
    uint32_t text_muted;   // very dim text
    uint32_t bar_bg;       // progress bar background
    uint32_t red;          // warnings/critical ("oel": ice cyan — the
                           // monochrome display has no red; mute reads
                           // as bright instead of red)
    uint32_t yellow;       // caution ("oel": joins the cyan family)
    uint32_t separator;    // separator line
    uint32_t accent;       // hot/active highlight (pulsing knob, ...)
    uint32_t zone_border;  // 1px frame around widget zones
    bool     zone_frame;   // draw the zone frames (OEL display windows)
    bool     terminal = false;  // phosphor CRT look ("green", "amber")

    // Unknown names fall back to "default" with a warning.
    static Theme fromName(const std::string& name);
};

namespace theme {
    // Active palette — same names widgets have always used.
    extern uint32_t bg;
    extern uint32_t green;
    extern uint32_t green_dim;
    extern uint32_t green_dark;
    extern uint32_t green_bg;
    extern uint32_t panel;
    extern uint32_t btn;
    extern uint32_t btn_border;
    extern uint32_t text;
    extern uint32_t text_dim;
    extern uint32_t text_muted;
    extern uint32_t bar_bg;
    extern uint32_t red;
    extern uint32_t yellow;
    extern uint32_t separator;
    extern uint32_t accent;
    extern uint32_t zone_border;
    extern bool     zone_frame;
    extern bool     terminal;

    void apply(const Theme& t);
    void apply(const Theme& t, const std::string& name);

    // Regenerate the active palette from a single hue, preserving each
    // slot's saturation/value (oel is a one-hue family). No-op unless
    // the active theme is "oel" — "default"'s red/yellow carry alert
    // semantics a hue sweep would break. Driven live by the colors
    // mode's main-hue slider.
    void retint(float hue);

    // Bumped by retint(); the layout engine watches this to force a
    // full redraw — statically-rendered widgets (buttons, transport)
    // would otherwise keep the old palette until their next state
    // change.
    extern uint32_t generation;

    // Name passed to apply(t, name), "" when unknown.
    extern std::string current_name;

    // Raised arcade/candy button chrome: face, 2px top highlight and
    // 2px bottom shade inside a rounded border — reads as a bezel
    // even on the monochrome OEL. border=0 uses theme::btn_border.
    void drawButton(Framebuffer& fb, int x, int y, int w, int h,
                    uint32_t border = 0);

    // Terminal themes only: recolors a finished widget to one phosphor
    // color by brightness and darkens every other row into scanlines,
    // so presets and album art join the look without knowing about it.
    void crt(Framebuffer& fb);

    // UI constants (not themed)
    constexpr int corner_radius    = 8;
    constexpr int btn_radius       = 10;
    constexpr int bar_radius       = 4;
    constexpr int widget_pad       = 6;  // internal padding
}
