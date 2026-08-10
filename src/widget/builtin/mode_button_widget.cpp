#include "widget/builtin/mode_button_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"

void ModeButtonWidget::onTap(int, int) {
    if (on_tap_) on_tap_();
}

void ModeButtonWidget::onLongPress(int, int) {
    if (on_long_press_) on_long_press_();
}

void ModeButtonWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    int w = fb.width();
    int h = fb.height();
    theme::drawButton(fb, 5, 6, w - 10, h - 12);

    // Hamburger glyph
    int cx = 5 + (w - 10) / 2;
    int cy = h / 2;
    for (int i = -1; i <= 1; ++i)
        fb.fillRoundRect(cx - 10, cy + i * 8 - 2, 20, 4, 2, theme::green);
}
