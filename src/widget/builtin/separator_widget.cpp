#include "widget/builtin/separator_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"

SeparatorWidget::SeparatorWidget() {
    name_ = "separator";
    requested_width_ = 10;
    dirty_ = true;
}

void SeparatorWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);
    int cx = fb.width() / 2;
    // Full-height thin green line
    fb.drawVLine(cx, 4, fb.height() - 8, theme::separator);
}
