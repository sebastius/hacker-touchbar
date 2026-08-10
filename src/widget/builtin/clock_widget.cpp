#include "widget/builtin/clock_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include <ctime>
#include <cstring>

ClockWidget::ClockWidget()
    : color_(theme::green)
{
    name_ = "clock";
    requested_width_ = 140;
    time_str_[0] = '\0';
}

bool ClockWidget::init() {
    update();
    return true;
}

void ClockWidget::update() {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);

    if (strcmp(buf, time_str_) != 0) {
        strncpy(time_str_, buf, sizeof(time_str_));
        markDirty();
    }
}

void ClockWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    // Render at 2x scale for readability
    const char* text = time_str_;
    int char_w = 8;
    int char_h = 16;
    int scale = 2;
    int text_len = static_cast<int>(strlen(text));
    int total_w = text_len * char_w * scale;
    int total_h = char_h * scale;

    int ox = (fb.width() - total_w) / 2;
    int oy = (fb.height() - total_h) / 2;

    // Full-height background pill
    fb.fillRoundRect(2, 2, fb.width() - 4, fb.height() - 4, 10, theme::green_bg);

    // Render 1x then scale up
    Framebuffer tmp(text_len * char_w, char_h);
    tmp.clear(0x00000000);
    tmp.drawText(0, 0, text, color_, FontSize::Normal);

    for (int y = 0; y < char_h; ++y) {
        for (int x = 0; x < text_len * char_w; ++x) {
            uint32_t px = tmp.getPixel(x, y);
            if (px != 0x00000000 && px != theme::bg) {
                fb.fillRect(ox + x * scale, oy + y * scale, scale, scale, px);
            }
        }
    }
}
