#include "widget/builtin/spacer_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"

SpacerWidget::SpacerWidget() {
    name_ = "spacer";
    requested_width_ = 0;
    dirty_ = true;
}

void SpacerWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);
}
