#include "engine/event_loop.hpp"
#include "util/log.hpp"
#include <sys/epoll.h>
#include <unistd.h>

static constexpr int MAX_EVENTS = 16;

EventLoop::EventLoop() : running_(false) {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        log_error("epoll_create1 failed");
    }
}

EventLoop::~EventLoop() {
    if (epoll_fd_ >= 0) close(epoll_fd_);
}

bool EventLoop::addFd(int fd, uint32_t events, Callback cb) {
    struct epoll_event ev = {};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        log_error("epoll_ctl ADD failed for fd %d", fd);
        return false;
    }
    callbacks_[fd] = std::move(cb);
    return true;
}

void EventLoop::removeFd(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    callbacks_.erase(fd);
}

void EventLoop::run() {
    running_ = true;
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            log_error("epoll_wait failed");
            break;
        }
        for (int i = 0; i < n; ++i) {
            auto it = callbacks_.find(events[i].data.fd);
            if (it != callbacks_.end()) {
                it->second(events[i].events);
            }
        }
    }
}

void EventLoop::stop() {
    running_ = false;
}
