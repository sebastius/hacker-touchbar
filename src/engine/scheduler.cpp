#include "engine/scheduler.hpp"
#include "engine/event_loop.hpp"
#include "output/output.hpp"
#include "gfx/framebuffer.hpp"
#include "widget/layout_engine.hpp"
#include "util/timer_fd.hpp"
#include "util/log.hpp"
#include <sys/epoll.h>
#include <unistd.h>

Scheduler::Scheduler(EventLoop& loop, Output& output, LayoutEngine& layout,
                     Framebuffer& composite, int coalesce_ms)
    : loop_(loop), output_(output), layout_(layout),
      composite_(composite), coalesce_ms_(coalesce_ms)
{
    // Create a one-shot timerfd (not armed yet)
    timer_fd_ = timerfd_create_ms(0, false);
    if (timer_fd_ >= 0) {
        loop_.addFd(timer_fd_, EPOLLIN, [this](uint32_t) {
            timerfd_read(timer_fd_);
            flush();
        });
    }
}

Scheduler::~Scheduler() {
    if (timer_fd_ >= 0) {
        loop_.removeFd(timer_fd_);
        close(timer_fd_);
    }
}

void Scheduler::markDirty() {
    dirty_ = true;
    if (!timer_armed_ && timer_fd_ >= 0) {
        timerfd_set_ms(timer_fd_, coalesce_ms_, false);
        timer_armed_ = true;
    }
}

void Scheduler::flush() {
    timer_armed_ = false;
    if (!dirty_) return;
    dirty_ = false;

    layout_.render(composite_);
    output_.present(composite_);
}
