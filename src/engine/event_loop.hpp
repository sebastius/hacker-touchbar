#pragma once
#include <functional>
#include <unordered_map>
#include <cstdint>

class EventLoop {
public:
    using Callback = std::function<void(uint32_t events)>;

    EventLoop();
    ~EventLoop();

    // Register an fd for epoll. Returns true on success.
    bool addFd(int fd, uint32_t events, Callback cb);
    void removeFd(int fd);

    void run();
    void stop();
    bool running() const { return running_; }

private:
    int epoll_fd_;
    bool running_;
    std::unordered_map<int, Callback> callbacks_;
};
