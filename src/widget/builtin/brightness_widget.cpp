#include "widget/builtin/brightness_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include "util/log.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <unistd.h>

BrightnessWidget::BrightnessWidget() : SliderWidget(100, 0) {
    name_ = "brightness";
    // requested_width_ = 0: flexible — fills the sliders mode
}

static std::string find_backlight() {
    DIR* dir = opendir("/sys/class/backlight");
    if (!dir) return "";
    std::string result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        // Never the touch bar's own backlight — a slider that can blank
        // the very screen it lives on is a self-own (and its max is 2,
        // so it isn't a display backlight anyway).
        if (strncmp(ent->d_name, "appletb", 7) == 0) continue;
        result = std::string("/sys/class/backlight/") + ent->d_name;
        break;
    }
    closedir(dir);
    return result;
}

bool BrightnessWidget::available() {
    return !find_backlight().empty();
}

static int read_int(const std::string& path, int fallback) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return fallback;
    int v = fallback;
    if (fscanf(f, "%d", &v) != 1) v = fallback;
    fclose(f);
    return v;
}

bool BrightnessWidget::init() {
    dev_path_ = find_backlight();
    if (dev_path_.empty()) {
        log_warn("Brightness: no /sys/class/backlight device");
        return false;
    }
    max_ = std::max(1, read_int(dev_path_ + "/max_brightness", 1));
    writable_ = access((dev_path_ + "/brightness").c_str(), W_OK) == 0;
    if (!writable_)
        log_warn("Brightness: %s not writable — display only", dev_path_.c_str());
    readCurrent();
    log_info("Brightness: %s (max %d%s)", dev_path_.c_str(), max_,
             writable_ ? "" : ", read-only");
    return true;
}

void BrightnessWidget::readCurrent() {
    setPercentFromBackend(read_int(dev_path_ + "/brightness", 0) * 100 / max_);
}

void BrightnessWidget::applyPercent(int pct) {
    if (!writable_) return;
    FILE* f = fopen((dev_path_ + "/brightness").c_str(), "w");
    if (!f) {
        log_warn("Brightness: write failed");
        return;
    }
    fprintf(f, "%d", pct * max_ / 100);
    fclose(f);
}

void BrightnessWidget::update() {
    readCurrent();
}

uint32_t BrightnessWidget::fillColor() const {
    return writable_ ? theme::yellow : theme::text_muted;
}

uint32_t BrightnessWidget::knobColor() const {
    return writable_ ? theme::yellow : theme::text_muted;
}

uint32_t BrightnessWidget::textColor() const {
    return writable_ ? theme::yellow : theme::text_muted;
}

void BrightnessWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    int h = fb.height();
    int btn_y = 2, btn_h = h - 4;

    // Sun pill (passive icon)
    fb.fillRoundRect(6, btn_y, ADORN_W - 12, btn_h, theme::btn_radius,
                     theme::btn);
    fb.drawRoundRect(6, btn_y, ADORN_W - 12, btn_h, theme::btn_radius,
                     theme::btn_border);
    uint32_t glyph = writable_ ? theme::yellow : theme::text_muted;
    int cx = 6 + (ADORN_W - 12) / 2;
    int cy = h / 2;
    fb.fillCircle(cx, cy, 4, glyph);
    fb.drawVLine(cx, cy - 9, 3, glyph);   // rays
    fb.drawVLine(cx, cy + 7, 3, glyph);
    fb.drawHLine(cx - 9, cy, 3, glyph);
    fb.drawHLine(cx + 7, cy, 3, glyph);
    fb.drawLine(cx - 6, cy - 6, cx - 5, cy - 5, glyph);
    fb.drawLine(cx + 5, cy + 5, cx + 6, cy + 6, glyph);
    fb.drawLine(cx + 5, cy - 6, cx + 6, cy - 5, glyph);
    fb.drawLine(cx - 6, cy + 5, cx - 5, cy + 6, glyph);

    drawSlider(fb);
}
