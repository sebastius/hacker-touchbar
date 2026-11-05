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
    return f ? v : -1;
}

void BarBacklight::set(int level) {
    std::ofstream f(kPath);
    f << level << "\n";
    f.flush();
    if (!f) log_warn("Could not set Touch Bar brightness to %d", level);
}
