#include "util/timer_fd.hpp"
#include "util/log.hpp"
#include <sys/timerfd.h>
#include <unistd.h>

int timerfd_create_ms(int interval_ms, bool repeating) {
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        log_error("timerfd_create failed");
        return -1;
    }
    timerfd_set_ms(fd, interval_ms, repeating);
    return fd;
}

uint64_t timerfd_read(int fd) {
    uint64_t count = 0;
    ssize_t n = read(fd, &count, sizeof(count));
    if (n != sizeof(count)) return 0;
    return count;
}

void timerfd_set_ms(int fd, int interval_ms, bool repeating) {
    struct itimerspec spec = {};
    spec.it_value.tv_sec = interval_ms / 1000;
    spec.it_value.tv_nsec = (interval_ms % 1000) * 1000000L;
    if (repeating) {
        spec.it_interval = spec.it_value;
    }
    timerfd_settime(fd, 0, &spec, nullptr);
}
