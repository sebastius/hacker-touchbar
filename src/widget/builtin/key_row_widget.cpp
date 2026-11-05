#include "widget/builtin/key_row_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include <glob.h>
#include <cstdio>
#include <ctime>

static constexpr int GAP = 4;
static constexpr int TEXT_SCALE = 3;  // 8x16 font: 48px tall on a 60px bar

static std::string read_clock() {
    char buf[16];
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%H:%M", &tm);
    return buf;
}

static std::string read_battery() {
    std::string out = "--%";
    glob_t g;
    if (glob("/sys/class/power_supply/*/capacity", 0, nullptr, &g) == 0) {
        if (FILE* f = fopen(g.gl_pathv[0], "r")) {
            int pct = 0;
            if (fscanf(f, "%d", &pct) == 1) out = std::to_string(pct) + "%";
            fclose(f);
        }
    }
    globfree(&g);
    return out;
}

static void draw_big_text(Framebuffer& fb, int cx, int cy,
                          const std::string& text, uint32_t color) {
    const int tw = fb.textWidth(text);
    if (tw <= 0) return;
    Framebuffer tmp(tw, 16);
    tmp.clear(0);
    tmp.drawText(0, 0, text, color);
    const int x0 = cx - tw * TEXT_SCALE / 2, y0 = cy - 8 * TEXT_SCALE;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < tw; ++x)
            if (tmp.getPixel(x, y))
                fb.fillRect(x0 + TEXT_SCALE * x, y0 + TEXT_SCALE * y,
                            TEXT_SCALE, TEXT_SCALE, color);
}

KeyRowWidget::KeyRowWidget(std::vector<Key> keys, Sink sink)
    : keys_(std::move(keys)), sink_(std::move(sink)),
      clock_(read_clock()), battery_(read_battery()) {
    name_ = "keyrow";
    for (const Key& k : keys_) total_stretch_ += k.stretch;
}

void KeyRowWidget::update() {
    std::string c = read_clock(), b = read_battery();
    if (c != clock_ || b != battery_) {
        clock_ = c;
        battery_ = b;
        markDirty();
    }
}

void KeyRowWidget::keySpan(int index, int width, int& x, int& w) const {
    int before = 0;
    for (int i = 0; i < index; ++i) before += keys_[i].stretch;
    int x0 = before * width / total_stretch_;
    int x1 = (before + keys_[index].stretch) * width / total_stretch_;
    x = x0 + GAP / 2;
    w = x1 - x0 - GAP;
}

int KeyRowWidget::keyAt(int x) const {
    int width = allocatedWidth();
    if (width <= 0 || total_stretch_ <= 0 || x < 0 || x >= width) return -1;
    for (int i = 0; i < static_cast<int>(keys_.size()); ++i) {
        int kx, kw;
        keySpan(i, width, kx, kw);
        // Gaps belong to the key on their left: no dead strips.
        if (x < kx + kw + GAP) return i;
    }
    return -1;
}

void KeyRowWidget::onPress(int x, int) {
    int i = keyAt(x);
    if (i < 0) return;
    const Key& k = keys_[i];
    if (k.codes.empty() && !k.action) return;
    if (sink_)
        for (int code : k.codes) sink_(code, true);
    if (k.action) k.action();
    pressed_ = i;
    markDirty();
}

void KeyRowWidget::onRelease() {
    if (pressed_ < 0) return;
    const auto& codes = keys_[pressed_].codes;
    if (sink_)
        for (auto it = codes.rbegin(); it != codes.rend(); ++it)
            sink_(*it, false);
    pressed_ = -1;
    markDirty();
}

std::string KeyRowWidget::textOf(const Key& k) const {
    if (k.text) return k.text();
    switch (k.show) {
    case Show::Clock:   return clock_;
    case Show::Battery: return battery_;
    default:            return k.label;
    }
}

void KeyRowWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);
    const int h = fb.height();
    for (int i = 0; i < static_cast<int>(keys_.size()); ++i) {
        int x, w;
        keySpan(i, fb.width(), x, w);
        const Key& k = keys_[i];
        const bool live = !k.codes.empty() || k.action;
        if (live) theme::drawButton(fb, x, 1, w, h - 2);
        // Pressed = reverse video.
        if (i == pressed_) fb.fillRect(x, 1, w, h - 2, theme::text);
        draw_big_text(fb, x + w / 2, h / 2, textOf(k),
                      i == pressed_ ? theme::bg
                                    : live ? theme::text : theme::text_dim);
    }
}
