#include "widget/builtin/battery_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include <cstdio>
#include <cstring>

BatteryWidget::BatteryWidget()
    : percent_(0), charging_(false),
      color_good_(theme::green),
      color_warn_(theme::yellow),
      color_crit_(theme::red)
{
    name_ = "battery";
    requested_width_ = 120;
}

bool BatteryWidget::init() {
    update();
    return true;
}

int BatteryWidget::readCapacity() {
    FILE* f = fopen("/sys/class/power_supply/BAT0/capacity", "r");
    if (!f) return -1;
    int cap = 0;
    if (fscanf(f, "%d", &cap) != 1) cap = -1;
    fclose(f);
    return cap;
}

bool BatteryWidget::readCharging() {
    FILE* f = fopen("/sys/class/power_supply/BAT0/status", "r");
    if (!f) return false;
    char buf[32] = {};
    if (fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return strncmp(buf, "Charging", 8) == 0 || strncmp(buf, "Full", 4) == 0;
    }
    fclose(f);
    return false;
}

void BatteryWidget::update() {
    int new_pct = readCapacity();
    bool new_chg = readCharging();
    if (new_pct != percent_ || new_chg != charging_) {
        percent_ = new_pct;
        charging_ = new_chg;
        markDirty();
    }
}

void BatteryWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    uint32_t bar_color = color_good_;
    if (percent_ < 10) bar_color = color_crit_;
    else if (percent_ < 30) bar_color = color_warn_;
    if (charging_) bar_color = theme::green_dim;

    int w = fb.width();
    int h = fb.height();

    // Full-height rounded panel
    fb.fillRoundRect(2, 2, w - 4, h - 4, 8, theme::green_bg);

    // Percentage text — larger
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", percent_);
    int tw = fb.textWidth(buf, FontSize::Normal);
    fb.drawText((w - tw) / 2, 4, buf, theme::text, FontSize::Normal);

    if (charging_) {
        fb.drawText((w + tw) / 2 + 2, 4, "+", theme::green_dim, FontSize::Normal);
    }

    // Thick rounded progress bar filling bottom half
    int bar_x = 8;
    int bar_y = h - 20;
    int bar_w = w - 16;
    int bar_h = 14;

    fb.fillRoundRect(bar_x, bar_y, bar_w, bar_h, 6, theme::bar_bg);
    int fill = bar_w * percent_ / 100;
    if (fill > 6)
        fb.fillRoundRect(bar_x, bar_y, fill, bar_h, 6, bar_color);
    else if (fill > 0)
        fb.fillRoundRect(bar_x, bar_y, 6, bar_h, 6, bar_color);
}
