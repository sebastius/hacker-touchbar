#include "widget/builtin/sysmon_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include <cstdio>
#include <cstring>

SysmonWidget::SysmonWidget()
    : cpu_percent_(0), ram_percent_(0), temp_celsius_(0),
      prev_total_(0), prev_idle_(0)
{
    name_ = "sysmon";
    requested_width_ = 340;
}

bool SysmonWidget::init() {
    readCpu();
    readRam();
    readTemp();
    return true;
}

void SysmonWidget::readCpu() {
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return;
    char line[256];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }
    fclose(f);
    long user, nice, system, idle, iowait, irq, softirq, steal;
    if (sscanf(line, "cpu %ld %ld %ld %ld %ld %ld %ld %ld",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4)
        return;
    long total = user + nice + system + idle + iowait + irq + softirq + steal;
    long total_diff = total - prev_total_;
    long idle_diff = idle - prev_idle_;
    if (total_diff > 0)
        cpu_percent_ = static_cast<int>(100 * (total_diff - idle_diff) / total_diff);
    prev_total_ = total;
    prev_idle_ = idle;
}

void SysmonWidget::readRam() {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return;
    long total = 0, available = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%ld", &total);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, "%ld", &available);
    }
    fclose(f);
    if (total > 0)
        ram_percent_ = static_cast<int>(100 * (total - available) / total);
}

void SysmonWidget::readTemp() {
    FILE* f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return;
    int raw = 0;
    if (fscanf(f, "%d", &raw) == 1)
        temp_celsius_ = raw / 1000;
    fclose(f);
}

void SysmonWidget::update() {
    int old_cpu = cpu_percent_, old_ram = ram_percent_, old_temp = temp_celsius_;
    readCpu();
    readRam();
    readTemp();
    if (cpu_percent_ != old_cpu || ram_percent_ != old_ram || temp_celsius_ != old_temp)
        markDirty();
}

void SysmonWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    int h = fb.height();
    int section_w = fb.width() / 3;
    int pad = 6;
    int bar_h = 14;

    auto drawMetric = [&](int section, const char* label, int value, bool is_temp) {
        int x = section * section_w + pad;
        int bar_w = section_w - pad * 2;

        // Label
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d%s", label, value, is_temp ? "C" : "%");
        fb.drawText(x + 2, 4, buf, theme::text_dim, FontSize::Small);

        // Rounded progress bar — pushed down to fill bottom
        int bar_y = h - bar_h - 6;
        fb.fillRoundRect(x, bar_y, bar_w, bar_h, theme::bar_radius, theme::bar_bg);

        int pct = is_temp ? ((value - 30) * 100 / 70) : value;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        int fill = bar_w * pct / 100;
        if (fill < bar_h) fill = (pct > 0) ? bar_h : 0;  // min width for rounded end

        uint32_t c = theme::green;
        int warn = is_temp ? 60 : 50;
        int crit = is_temp ? 80 : 80;
        if (value > crit) c = theme::red;
        else if (value > warn) c = theme::yellow;

        if (fill > 0)
            fb.fillRoundRect(x, bar_y, fill, bar_h, theme::bar_radius, c);
    };

    drawMetric(0, "CPU", cpu_percent_, false);
    drawMetric(1, "RAM", ram_percent_, false);
    drawMetric(2, "TMP", temp_celsius_, true);
}
