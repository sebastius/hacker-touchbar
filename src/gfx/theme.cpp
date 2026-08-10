#include "gfx/theme.hpp"
#include "gfx/color.hpp"
#include "gfx/framebuffer.hpp"
#include "util/log.hpp"

// Active palette, initialized to "default" so tests and any code path
// that never calls theme::apply keep the original Matrix green look.
namespace theme {
    uint32_t bg          = 0xFF000000;  // pure black
    uint32_t green       = 0xFF00FF41;  // neon green #00ff41
    uint32_t green_dim   = 0xFF00CC33;  // dimmer green #00cc33
    uint32_t green_dark  = 0xFF003B00;  // dark green #003b00
    uint32_t green_bg    = 0xFF0A1A0A;  // very subtle green-black for panels
    uint32_t panel       = 0xFF111611;  // slightly lighter panel bg
    uint32_t btn         = 0xFF0D2B0D;  // button background
    uint32_t btn_border  = 0xFF004400;  // button border
    uint32_t text        = 0xFF00FF41;  // primary text
    uint32_t text_dim    = 0xFF009926;  // secondary/dim text
    uint32_t text_muted  = 0xFF004D15;  // very dim text
    uint32_t bar_bg      = 0xFF0A1A0A;  // progress bar background
    uint32_t red         = 0xFFFF0000;  // warnings/critical
    uint32_t yellow      = 0xFFCCCC00;  // caution
    uint32_t separator   = 0xFF003B00;  // separator line
    uint32_t accent      = 0xFFFFFFFF;  // hot/active highlight
    uint32_t zone_border = 0xFF004400;  // 1px frame around widget zones
    bool     zone_frame  = false;       // zone frames off by default
    uint32_t generation  = 0;           // bumps on every retint
    std::string current_name;

    void apply(const Theme& t) { apply(t, ""); }

    void apply(const Theme& t, const std::string& name) {
        bg          = t.bg;
        green       = t.green;
        green_dim   = t.green_dim;
        green_dark  = t.green_dark;
        green_bg    = t.green_bg;
        panel       = t.panel;
        btn         = t.btn;
        btn_border  = t.btn_border;
        text        = t.text;
        text_dim    = t.text_dim;
        text_muted  = t.text_muted;
        bar_bg      = t.bar_bg;
        red         = t.red;
        yellow      = t.yellow;
        separator   = t.separator;
        accent      = t.accent;
        zone_border = t.zone_border;
        zone_frame  = t.zone_frame;
        current_name = name;
    }

    void retint(float hue) {
        if (current_name != "oel") return;
        ++generation;
        // White (the colors slider's >360 zone) desaturates the whole
        // family to a grayscale ramp.
        const float s = (hue > 360.0f) ? 0.0f : 1.0f;
        // oel slots are one hue family; each keeps its own (s, v).
        green       = hsv_to_rgb(hue, 0.82f * s, 1.00f);
        green_dim   = hsv_to_rgb(hue, 0.85f * s, 0.77f);
        green_dark  = hsv_to_rgb(hue, 0.87f * s, 0.30f);
        green_bg    = hsv_to_rgb(hue, 0.81f * s, 0.12f);
        panel       = hsv_to_rgb(hue, 0.50f * s, 0.09f);
        btn         = hsv_to_rgb(hue, 0.82f * s, 0.17f);
        btn_border  = hsv_to_rgb(hue, 0.83f * s, 0.42f);
        text        = hsv_to_rgb(hue, 0.46f * s, 1.00f);
        text_dim    = hsv_to_rgb(hue, 0.74f * s, 0.71f);
        text_muted  = hsv_to_rgb(hue, 0.77f * s, 0.39f);
        bar_bg      = hsv_to_rgb(hue, 0.81f * s, 0.12f);
        red         = hsv_to_rgb(hue, 0.28f * s, 1.00f);
        yellow      = hsv_to_rgb(hue, 0.82f * s, 1.00f);
        separator   = hsv_to_rgb(hue, 0.87f * s, 0.30f);
        accent      = hsv_to_rgb(hue, 0.08f * s, 1.00f);
        zone_border = hsv_to_rgb(hue, 0.83f * s, 0.42f);
    }

    void drawButton(Framebuffer& fb, int x, int y, int w, int h,
                    uint32_t border) {
        const int r = btn_radius < (h - 2) / 2 ? btn_radius : (h - 2) / 2;
        fb.fillRoundRect(x, y, w, h, r, btn);
        // Bezel: light catches the top edge, shade pools at the bottom.
        const int inset = r / 2 + 3;
        if (w > 2 * inset + 4 && h > 10) {
            fb.drawHLine(x + inset, y + 2, w - 2 * inset, green_dim);
            fb.drawHLine(x + inset, y + 3, w - 2 * inset, green_dim);
            fb.drawHLine(x + inset, y + h - 3, w - 2 * inset, green_dark);
            fb.drawHLine(x + inset, y + h - 4, w - 2 * inset, green_dark);
        }
        fb.drawRoundRect(x, y, w, h, r, border ? border : btn_border);
    }
}

Theme Theme::fromName(const std::string& name) {
    if (name == "oel") {
        // Pioneer DEH OEL: monochrome electric cyan on black (hue
        // 190-202, same family as the dolphins/spectrum presets).
        Theme t;
        t.bg          = 0xFF000000;
        t.green       = 0xFF2FD8FF;  // primary cyan
        t.green_dim   = 0xFF1E9CC4;  // dim cyan
        t.green_dark  = 0xFF0A3D4D;  // dark cyan outline
        t.green_bg    = 0xFF06181F;  // cyan-tinted black panels
        t.panel       = 0xFF0C1418;
        t.btn         = 0xFF08222C;  // button background
        t.btn_border  = 0xFF12536B;  // button border
        t.text        = 0xFF8AE9FF;  // light cyan text
        t.text_dim    = 0xFF2E96B4;  // dim cyan text
        t.text_muted  = 0xFF174E63;  // very dim cyan text
        t.bar_bg      = 0xFF06181F;  // progress bar background
        t.red         = 0xFFB8F1FF;  // "alert" = ice cyan (no red on OEL)
        t.yellow      = 0xFF2FD8FF;  // brightness joins the cyan family
        t.separator   = 0xFF0A3D4D;
        t.accent      = 0xFFEAFBFF;  // white-cyan hot highlight
        t.zone_border = 0xFF12536B;  // display-window frames
        t.zone_frame  = true;
        return t;
    }

    if (name != "default")
        log_warn("Unknown ui theme '%s' — falling back to 'default'",
                 name.c_str());

    // Matrix / Omarchy Aether neon green (matches the theme:: defaults)
    Theme t;
    t.bg          = 0xFF000000;
    t.green       = 0xFF00FF41;
    t.green_dim   = 0xFF00CC33;
    t.green_dark  = 0xFF003B00;
    t.green_bg    = 0xFF0A1A0A;
    t.panel       = 0xFF111611;
    t.btn         = 0xFF0D2B0D;
    t.btn_border  = 0xFF004400;
    t.text        = 0xFF00FF41;
    t.text_dim    = 0xFF009926;
    t.text_muted  = 0xFF004D15;
    t.bar_bg      = 0xFF0A1A0A;
    t.red         = 0xFFFF0000;
    t.yellow      = 0xFFCCCC00;
    t.separator   = 0xFF003B00;
    t.accent      = 0xFFFFFFFF;
    t.zone_border = 0xFF004400;
    t.zone_frame  = false;
    return t;
}
