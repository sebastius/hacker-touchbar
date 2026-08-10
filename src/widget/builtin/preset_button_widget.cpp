#include "widget/builtin/preset_button_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"

void PresetButtonWidget::onTap(int, int) {
    if (on_tap_) on_tap_();
}

void PresetButtonWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    int w = fb.width();
    int h = fb.height();
    theme::drawButton(fb, 3, 6, w - 6, h - 12);

    // Four-point sparkle glyph
    int cx = 3 + (w - 6) / 2;
    int cy = h / 2;
    fb.drawVLine(cx, cy - 9, 19, theme::green);   // vertical
    fb.drawHLine(cx - 9, cy, 19, theme::green);   // horizontal
    for (int i = 1; i <= 4; ++i) {                // diagonal tips
        fb.setPixel(cx - i, cy - (5 - i), theme::green_dim);
        fb.setPixel(cx + i, cy - (5 - i), theme::green_dim);
        fb.setPixel(cx - i, cy + (5 - i), theme::green_dim);
        fb.setPixel(cx + i, cy + (5 - i), theme::green_dim);
    }
}
