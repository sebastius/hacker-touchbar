#include "widget/builtin/transport_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"

TransportWidget::TransportWidget(int width) {
    name_ = "transport";
    requested_width_ = width;
}

void TransportWidget::sync(const MprisState& state) {
    if (state.has_player != has_player_ || state.playing != playing_) {
        has_player_ = state.has_player;
        playing_ = state.playing;
        markDirty();
    }
}

void TransportWidget::onTap(int x, int) {
    if (!client_ || !has_player_) return;
    int w = allocatedWidth();
    int pill = (w - 2 * PAD - 2 * GAP) / 3;
    int x0 = PAD, x1 = x0 + pill + GAP, x2 = x1 + pill + GAP;
    if (x >= x0 && x < x0 + pill) client_->previous();
    else if (x >= x1 && x < x1 + pill) client_->playPause();
    else if (x >= x2 && x < x2 + pill) client_->next();
}

void TransportWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    int w = fb.width();
    int h = fb.height();
    int btn_y = 6;
    int btn_h = h - 12;
    int pill = (w - 2 * PAD - 2 * GAP) / 3;
    int x0 = PAD, x1 = x0 + pill + GAP, x2 = x1 + pill + GAP;

    // Playing = active state (in "oel" theme::text is a lighter cyan);
    // paused with a player / no player = accent / muted.
    uint32_t glyph = has_player_ ? (playing_ ? theme::text : theme::green)
                                 : theme::text_muted;
    uint32_t border = has_player_ ? theme::btn_border : theme::panel;

    theme::drawButton(fb, x0, btn_y, pill, btn_h, border);
    theme::drawButton(fb, x1, btn_y, pill, btn_h, border);
    theme::drawButton(fb, x2, btn_y, pill, btn_h, border);

    int cy = h / 2;
    int s = 10;  // half glyph size

    // Previous: |<<  (bar on the left, two left-pointing chevrons —
    // vertex on the left, arms opening to the right)
    int pcx = x0 + pill / 2;
    fb.drawVLine(pcx - 14, cy - s, 2 * s + 1, glyph);
    for (int i = 0; i < s; ++i) {
        fb.setPixel(pcx - 10 + i, cy - i, glyph);
        fb.setPixel(pcx - 10 + i, cy + i, glyph);
        fb.setPixel(pcx - 2 + i, cy - i, glyph);
        fb.setPixel(pcx - 2 + i, cy + i, glyph);
    }

    // Play / Pause
    int ccx = x1 + pill / 2;
    if (has_player_ && playing_) {
        fb.fillRect(ccx - 7, cy - s, 5, 2 * s, glyph);
        fb.fillRect(ccx + 3, cy - s, 5, 2 * s, glyph);
    } else {
        for (int row = 0; row < 2 * s; ++row) {
            int lw = (row <= s) ? row * (s - 1) / s
                                : (2 * s - row) * (s - 1) / s;
            fb.drawHLine(ccx - 5, cy - s + row, lw + 1, glyph);
        }
    }

    // Next: >>|  (two right-pointing chevrons — vertex on the right —
    // bar on the right)
    int ncx = x2 + pill / 2;
    for (int i = 0; i < s; ++i) {
        fb.setPixel(ncx + 2 - i, cy - i, glyph);
        fb.setPixel(ncx + 2 - i, cy + i, glyph);
        fb.setPixel(ncx + 10 - i, cy - i, glyph);
        fb.setPixel(ncx + 10 - i, cy + i, glyph);
    }
    fb.drawVLine(ncx + 14, cy - s, 2 * s + 1, glyph);
}
