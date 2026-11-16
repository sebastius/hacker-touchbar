#include "util/bar_backlight.hpp"
#include "util/log.hpp"
#include <unistd.h>
#include <fstream>

static const char* const kPath =
    "/sys/class/backlight/appletb_backlight/brightness";

bool BarBacklight::available() const {
    return access(kPath, F_OK) == 0;
}

int BarBacklight::level() const {
    int v = -1;
    std::ifstream f(kPath);
    f >> v;
    if (!f) return -1;
    return v >= 2 && boost_ ? 3 : v;
}

void BarBacklight::set(int level) {
    boost_ = level >= 3;
    const int hardware = level >= 2 ? 2 : level;
    std::ofstream f(kPath);
    f << hardware << "\n";
    f.flush();
    if (!f) log_warn("Could not set Touch Bar brightness to %d", level);
}
