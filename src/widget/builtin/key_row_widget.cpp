#include "widget/builtin/key_row_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include "viz/viz_renderer.hpp"
#include <glob.h>
#include <algorithm>
#include <cstdio>
#include <ctime>

static constexpr int TEXT_SCALE = 3;  // 8x16 font: 48px tall on a 60px bar
static constexpr int KEY_PAD = 16;    // fit rows: space either side of the text
static constexpr int DOT_R = 15;      // radius of the theme dot

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

KeyRowWidget::KeyRowWidget(std::vector<Key> keys, Sink sink, bool fit, int gap)
    : keys_(std::move(keys)), sink_(std::move(sink)), fit_(fit), gap_(gap),
      clock_(read_clock()), battery_(read_battery()) {
    name_ = "keyrow";
    for (const Key& k : keys_) {
        total_stretch_ += k.stretch;
        has_blink_ = has_blink_ || k.blink;
    }
}

KeyRowWidget::~KeyRowWidget() = default;

void KeyRowWidget::setBackground(const Config& cfg, const std::string& preset) {
    bg_cfg_ = &cfg;
    bg_preset_ = preset;
}

void KeyRowWidget::update() {
    const int step = timerIntervalMs();
    elapsed_ms_ = (elapsed_ms_ + step) % 60000;
    auto every = [&](int ms) { return elapsed_ms_ % ms < step; };

    bool changed = bg_ != nullptr;  // an animation needs every frame
    if (every(250)) {
        std::string live;
        for (const Key& k : keys_)
            if (k.text) live += k.text() + '\n';
        changed = changed || live != live_labels_;
        live_labels_ = std::move(live);
    }
    if (has_blink_ && every(500)) {  // half a second on, half off
        blink_on_ = !blink_on_;
        changed = true;
    }
    if (every(1000)) {  // clock and battery can take their time
        std::string c = read_clock(), b = read_battery();
        changed = changed || c != clock_ || b != battery_;
        clock_ = c;
        battery_ = b;
    }
    if (changed || (!bg_preset_.empty() && !bg_)) markDirty();
}

int KeyRowWidget::fitWidth(const Key& k) const {
    if (k.show == Show::Gap) return k.min_px;
    const int content = k.show == Show::ThemeDot
        ? 2 * DOT_R
        : static_cast<int>((k.fit_text.empty() ? textOf(k) : k.fit_text).size()) * 8 * TEXT_SCALE;
    return std::max(content + 2 * KEY_PAD, k.min_px);
}

bool KeyRowWidget::fits(int width) const {
    int total = 0;
    for (const Key& k : keys_) total += fitWidth(k) + gap_;
    return fit_ && total <= width;
}

void KeyRowWidget::keySpan(int index, int width, int& x, int& w) const {
    if (!fits(width)) {  // too many keys to fit: share the width instead
        int before = 0;
        for (int i = 0; i < index; ++i) before += keys_[i].stretch;
        int x0 = before * width / total_stretch_;
        int x1 = (before + keys_[index].stretch) * width / total_stretch_;
        x = x0 + gap_ / 2;
        w = x1 - x0 - gap_;
        return;
    }

    int tail = static_cast<int>(keys_.size());  // first right-aligned key
    while (tail > 0 && keys_[tail - 1].codes.empty() && !keys_[tail - 1].action)
        --tail;

    int pos = gap_ / 2;
    if (index >= tail) {
        pos = width - gap_ / 2;
        for (int i = tail; i < static_cast<int>(keys_.size()); ++i)
            pos -= fitWidth(keys_[i]) + gap_;
        pos += gap_;
    }
    for (int i = index >= tail ? tail : 0; i < index; ++i)
        pos += fitWidth(keys_[i]) + gap_;
    x = pos;
    w = fitWidth(keys_[index]);
}

int KeyRowWidget::keyAt(int x) const {
    int width = allocatedWidth();
    if (width <= 0 || total_stretch_ <= 0 || x < 0 || x >= width) return -1;
    for (int i = 0; i < static_cast<int>(keys_.size()); ++i) {
        int kx, kw;
        keySpan(i, width, kx, kw);
        // Gaps belong to the key on their left: no dead strips. The
        // open space in a fit row belongs to nobody.
        if (x < kx + kw + gap_) return x >= kx - gap_ ? i : -1;
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

// The other theme's phosphor, so the dot shows what a tap switches to.
// Odd rows are dimmed to match the scanlines around it.
void KeyRowWidget::renderAfterCrt(Framebuffer& fb) {
    constexpr int R = DOT_R;
    const char* other = theme::current_name == "amber" ? "green" : "amber";
    const uint32_t bright = Theme::fromName(other).text;
    const uint32_t dim = 0xFF000000u | (((bright >> 16) & 0xFF) * 180 / 255) << 16 |
                         (((bright >> 8) & 0xFF) * 180 / 255) << 8 |
                         ((bright & 0xFF) * 180 / 255);
    for (int i = 0; i < static_cast<int>(keys_.size()); ++i) {
        if (keys_[i].show != Show::ThemeDot) continue;
        int x, w;
        keySpan(i, fb.width(), x, w);
        const int cx = x + w / 2, cy = fb.height() / 2;
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx)
                if (dx * dx + dy * dy <= R * R)
                    fb.setPixel(cx + dx, cy + dy, ((cy + dy) & 1) ? dim : bright);
    }
}

std::string KeyRowWidget::textOf(const Key& k) const {
    if (k.text) return k.text();
    switch (k.show) {
    case Show::Clock:   return clock_;
    case Show::Battery: return battery_;
    default:            return k.label;
    }
}

// Darkens what is behind a key, so its text stays readable.
static void dim_rect(Framebuffer& fb, int x, int y, int w, int h) {
    for (int py = y; py < y + h; ++py) {
        for (int px = x; px < x + w; ++px) {
            const uint32_t c = fb.getPixel(px, py);
            fb.setPixel(px, py, 0xFF000000u | ((c >> 2) & 0x3F3F3F));
        }
    }
}

void KeyRowWidget::render(Framebuffer& fb) {
    const int h = fb.height();
    if (!bg_preset_.empty()) {
        if (!bg_) bg_ = std::make_unique<VizRenderer>(fb.width(), h, *bg_cfg_, bg_preset_);
        bg_->render(fb);
    } else {
        fb.clear(theme::bg);
    }

    for (int i = 0; i < static_cast<int>(keys_.size()); ++i) {
        int x, w;
        keySpan(i, fb.width(), x, w);
        const Key& k = keys_[i];
        if (k.show == Show::Gap) continue;
        const bool live = !k.codes.empty() || k.action;

        if (bg_) {
            dim_rect(fb, x, 1, w, h - 2);
            if (live) fb.drawRect(x, 1, w, h - 2, theme::btn_border);
        } else if (live) {
            theme::drawButton(fb, x, 1, w, h - 2);
        }
        if (i == pressed_) fb.fillRect(x, 1, w, h - 2, theme::text);  // reverse video
        if (k.show == Show::ThemeDot) continue;  // drawn after the phosphor pass

        std::string text = textOf(k);
        if (k.blink && !blink_on_ && !text.empty()) text.back() = ' ';
        draw_big_text(fb, x + w / 2, h / 2, text,
                      i == pressed_ ? theme::bg
                                    : live ? theme::text : theme::text_dim);
    }
}

