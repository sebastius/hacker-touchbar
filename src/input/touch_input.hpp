#pragma once
#include <libevdev/libevdev.h>
#include <chrono>

class EventLoop;
class LayoutEngine;
class Widget;

// Multitouch input for the Touch Bar digitizer (hid-multitouch).
//
// Hardware facts (from raw evdev captures on the real device):
//  - TRACKING_ID / BTN_TOUCH are the authoritative down/up signals.
//  - evdev only emits an axis event when its value CHANGES, so a
//    cached last-seen X is always correct — even for a down frame
//    with no position events (same-X tap). This driver's EVIOCGABS
//    is broken (always reads 0), so caching is the ONLY source of
//    position: last_x_ persists across touches and is initialized to
//    the device center so a first-ever touch without X lands mid-bar.
//  - Y is NEVER emitted; hit-testing uses a constant mid-bar Y.
//  - Slot numbers wander; only a single finger is tracked.
// Events are evaluated once per evdev frame (SYN_REPORT).
class TouchInput {
public:
    TouchInput(EventLoop& loop, LayoutEngine& layout);
    ~TouchInput();

    bool init();
    void shutdown();

    void setAxisRangesForTest(int xmin, int xmax, int ymin, int ymax) {
        abs_x_min_ = xmin; abs_x_max_ = xmax;
        abs_y_min_ = ymin; abs_y_max_ = ymax;
        last_x_ = (xmin + xmax) / 2;  // device center, like init()
    }

    // Event entry points shared by the evdev read loop and the test
    // binary. POSITION events take raw axis values (mapped here).
    // Events only update state; gestures are evaluated once per evdev
    // frame in onSynReport().
    void onAbsEvent(int code, int raw_value);
    void onKeyEvent(int code, int value);
    void onSynReport();

    // Long-press check: fires onLongPress on the widget under the
    // finger when the press has been held >= LONG_PRESS_MS without
    // moving past tap slop. Called by the internal timer (production)
    // or directly by tests; safe to call spuriously.
    void longPressCheck();

    static constexpr int LONG_PRESS_MS = 600;

private:
    bool findDevice();
    void handleEvents();
    void onFingerUp();
    void onMotion();
    void recordStart(int x, int y);

    // Map raw evdev coordinates to logical pixel space
    int mapX(int raw);
    int mapY(int raw);

    EventLoop& loop_;
    LayoutEngine& layout_;
    struct libevdev* dev_;
    int fd_;

    // Axis ranges (read from device)
    int abs_x_min_, abs_x_max_;
    int abs_y_min_, abs_y_max_;

    // Cached last-seen raw X; persists across touches (see class
    // comment). Never cleared on touch-up.
    int last_x_ = 0;

    // Touch state
    enum class State { Idle, Down };
    State state_;
    bool pending_down_ = false;  // TRACKING_ID/BTN_TOUCH seen, awaiting SYN
    bool pending_up_ = false;
    int start_x_, start_y_;
    int cur_x_, cur_y_;
    std::chrono::steady_clock::time_point touch_start_;

    // Gesture classification (see input/gesture.hpp for thresholds)
    Widget* down_widget_ = nullptr;
    int down_local_x_ = 0;
    bool down_drag_capable_ = false;
    bool dragging_ = false;      // streaming onDrag to down_widget_
    bool gesture_fired_ = false; // long-press dispatched

    int longpress_fd_ = -1;      // one-shot timer armed at press start

    static constexpr int TAP_MAX_DIST = 20;       // pixels
    static constexpr int TAP_MAX_MS = 400;         // milliseconds
};
