#include "input/fn_watcher.hpp"
#include "engine/event_loop.hpp"
#include "util/log.hpp"
#include <dirent.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstdio>
#include <cerrno>
#include <cstring>

void FnTracker::onKey(int code, int value, long now_ms) {
    if (code != KEY_FN) {
        if (value == 1) used_ = true;
        return;
    }
    if (value == 1 && !down_) {
        down_ = true;
        used_ = false;
        down_ms_ = now_ms;
        if (on_hold) on_hold(true);
    } else if (value == 0 && down_) {
        down_ = false;
        // Tap first: the hold release then lands on the new layer
        // without flashing the animation in between.
        if (!used_ && now_ms - down_ms_ < TAP_MAX_MS && on_tap) on_tap();
        if (on_hold) on_hold(false);
    }
}

FnWatcher::~FnWatcher() {
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
        close(fd_);
    }
}

bool FnWatcher::init() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return false;
    while (struct dirent* ent = readdir(dir)) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        char path[280];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        struct libevdev* dev = nullptr;
        bool match = false;
        if (libevdev_new_from_fd(fd, &dev) == 0) {
            const char* name = libevdev_get_name(dev);
            match = name && strstr(name, "Internal Keyboard") &&
                    libevdev_has_event_code(dev, EV_KEY, KEY_FN);
            libevdev_free(dev);
        }
        if (match) {
            fd_ = fd;
            break;
        }
        close(fd);
    }
    closedir(dir);

    if (fd_ < 0) {
        log_warn("Keyboard with Fn key not found: Fn layers disabled");
        return false;
    }
    loop_.addFd(fd_, EPOLLIN, [this](uint32_t) { handleEvents(); });
    log_info("Fn key: watching internal keyboard");
    return true;
}

void FnWatcher::handleEvents() {
    struct input_event ev;
    for (;;) {
        ssize_t n = read(fd_, &ev, sizeof(ev));
        if (n != static_cast<ssize_t>(sizeof(ev))) {
            if (n < 0 && errno == ENODEV) {
                log_warn("Keyboard went away: Fn layers disabled");
                loop_.removeFd(fd_);
                close(fd_);
                fd_ = -1;
            }
            return;
        }
        if (ev.type != EV_KEY) continue;
        long ms = ev.input_event_sec * 1000L + ev.input_event_usec / 1000L;
        tracker_.onKey(ev.code, ev.value, ms);
    }
}
