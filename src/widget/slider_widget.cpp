#include "widget/slider_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include <algorithm>
#include <cstdio>

SliderWidget::SliderWidget(int max_percent, int width)
    : max_percent_(std::max(1, max_percent))
{
    requested_width_ = width;
}

int SliderWidget::percentForX(int x) const {
    int track_w = allocatedWidth() - TRACK_X0 - TRACK_PAD;
    if (track_w <= 0) return 0;
    int clamped = std::max(0, std::min(track_w, x - TRACK_X0));
    return clamped * max_percent_ / track_w;
}

void SliderWidget::setPercentLocal(int percent) {
    percent_ = std::max(0, std::min(max_percent_, percent));
    markDirty();
    applyPercent(percent_);
}

void SliderWidget::setPercentFromBackend(int percent) {
    percent = std::max(0, std::min(max_percent_, percent));
    if (percent != percent_) {
        percent_ = percent;
        markDirty();
    }
}

void SliderWidget::onTap(int x, int) {
    if (x >= TRACK_X0)
        setPercentLocal(percentForX(x));
}

void SliderWidget::onDrag(int x, int, DragPhase phase) {
    if (phase == DragPhase::End) return;  // last Move already applied
    if (phase == DragPhase::Begin) {
        // Grabbing the track itself jumps the knob to the finger
        // (absolute). Grabbing the dead zone left of the track keeps
        // the current value and follows relatively — no jump to 0.
        grab_offset_ = (x >= TRACK_X0) ? 0 : percent_;
    }
    setPercentLocal(percentForX(x) + grab_offset_);
}

void SliderWidget::drawSlider(Framebuffer& fb) {
    int w = fb.width();
    int h = fb.height();

    int track_w = w - TRACK_X0 - TRACK_PAD;
    if (track_w <= 0) return;
    int track_y = (h - TRACK_H) / 2;
    if (theme::terminal) {  // level meter of solid blocks, no knob
        const int pitch = 11, bh = h - 24;
        const int blocks = track_w / pitch;
        const int lit = blocks * std::min(percent_, max_percent_) / max_percent_;
        for (int i = 0; i < blocks; ++i)
            fb.fillRect(TRACK_X0 + i * pitch, 12, pitch - 3, bh,
                        i < lit ? fillColor() : theme::bar_bg);
        return;
    }
    fb.fillRoundRect(TRACK_X0, track_y, track_w, TRACK_H, 8, theme::bar_bg);
    int fill = track_w * std::min(percent_, max_percent_) / max_percent_;
    if (fill > 0)
        fb.fillRoundRect(TRACK_X0, track_y, fill, TRACK_H, 8, fillColor());

    // Handle knob: bright dot with a subtle outline at the fill edge
    int kx = TRACK_X0 + fill;
    int ky = h / 2;
    if (theme::terminal) {  // block cursor instead of a round knob
        fb.fillRect(kx - 5, ky - 12, 10, 24, knobColor());
    } else {
        fb.fillCircle(kx, ky, 10, theme::green_dark);  // outline
        fb.fillCircle(kx, ky, 8, knobColor());         // dot
    }

    // Percent readout on the track's right end (dark backdrop so the
    // knob can't swallow it at high values)
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", percent_);
    int tw = fb.textWidth(buf, FontSize::Small);
    fb.fillRoundRect(w - TRACK_PAD - tw - 8, (h - 14) / 2, tw + 8, 14, 4,
                     theme::bg);
    fb.drawText(w - TRACK_PAD - tw - 4, (h - 10) / 2, buf, textColor(),
                FontSize::Small);
}
