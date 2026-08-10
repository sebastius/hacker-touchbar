#pragma once

class EventLoop;
class Output;
class Framebuffer;

class LayoutEngine;

class Scheduler {
public:
    Scheduler(EventLoop& loop, Output& output, LayoutEngine& layout,
              Framebuffer& composite, int coalesce_ms = 16);
    ~Scheduler();

    // Called by widgets when they change.
    void markDirty();

    // Called internally when coalesce timer fires.
    void flush();

    int coalesceMs() const { return coalesce_ms_; }

private:
    EventLoop& loop_;
    Output& output_;
    LayoutEngine& layout_;
    Framebuffer& composite_;
    int coalesce_ms_;
    int timer_fd_;
    bool dirty_ = false;
    bool timer_armed_ = false;
};
