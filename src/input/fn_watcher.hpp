#pragma once
#include <functional>

class EventLoop;

// Turns raw key events into "Fn held" and "Fn tapped". A tap is a
// short press with no other key or Touch Bar button used meanwhile,
// so Fn+arrow chords never count. Separate from the device code so
// tests can feed it events.
class FnTracker {
public:
    static constexpr long TAP_MAX_MS = 300;

    std::function<void(bool)> on_hold;
    std::function<void()> on_tap;

    void onKey(int code, int value, long now_ms);
    void markUsed() { used_ = true; }

private:
    bool down_ = false;
    bool used_ = false;
    long down_ms_ = 0;
};

// Watches the internal keyboard for Fn. The device is not grabbed and
// no key other than Fn is stored: other keys only cancel a tap.
class FnWatcher {
public:
    explicit FnWatcher(EventLoop& loop) : loop_(loop) {}
    ~FnWatcher();

    bool init();
    FnTracker& tracker() { return tracker_; }

private:
    void handleEvents();

    EventLoop& loop_;
    FnTracker tracker_;
    int fd_ = -1;
};
