#include "input/touch_input.hpp"
#include "input/gesture.hpp"
#include "engine/event_loop.hpp"
#include "widget/layout_engine.hpp"
#include "widget/widget.hpp"
#include "util/log.hpp"
#include "util/timer_fd.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <cstdlib>
#include <sys/epoll.h>
#include <linux/input.h>

TouchInput::TouchInput(EventLoop& loop, LayoutEngine& layout)
    : loop_(loop), layout_(layout), dev_(nullptr), fd_(-1),
      abs_x_min_(0), abs_x_max_(1), abs_y_min_(0), abs_y_max_(1),
      state_(State::Idle), start_x_(0), start_y_(0), cur_x_(0), cur_y_(0)
{}

TouchInput::~TouchInput() {
    shutdown();
}

bool TouchInput::init() {
    if (!findDevice()) {
        log_warn("Touch Bar input device not found — touch disabled");
        return false;
    }

    // Read axis ranges
    const struct input_absinfo* abs_x = libevdev_get_abs_info(dev_, ABS_MT_POSITION_X);
    const struct input_absinfo* abs_y = libevdev_get_abs_info(dev_, ABS_MT_POSITION_Y);
    if (abs_x) {
        abs_x_min_ = abs_x->minimum;
        abs_x_max_ = abs_x->maximum;
    }
    if (abs_y) {
        abs_y_min_ = abs_y->minimum;
        abs_y_max_ = abs_y->maximum;
    }

    log_info("Touch input: %s (X: %d-%d, Y: %d-%d)",
             libevdev_get_name(dev_),
             abs_x_min_, abs_x_max_, abs_y_min_, abs_y_max_);

    // A first-ever touch without any X event lands mid-bar, not at 0.
    last_x_ = (abs_x_min_ + abs_x_max_) / 2;

    // Register fd on event loop
    loop_.addFd(fd_, EPOLLIN, [this](uint32_t) {
        handleEvents();
    });

    // Long-press timer (one-shot, armed when a press starts)
    longpress_fd_ = timerfd_create_ms(0, false);
    if (longpress_fd_ >= 0) {
        loop_.addFd(longpress_fd_, EPOLLIN, [this](uint32_t) {
            timerfd_read(longpress_fd_);
            longPressCheck();
        });
    }

    return true;
}

void TouchInput::shutdown() {
    if (longpress_fd_ >= 0) {
        loop_.removeFd(longpress_fd_);
        close(longpress_fd_);
        longpress_fd_ = -1;
    }
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
    }
    if (dev_) {
        libevdev_free(dev_);
        dev_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool TouchInput::findDevice() {
    DIR* dir = opendir("/dev/input");
    if (!dir) return false;

    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;

        char path[280];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);

        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        struct libevdev* dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) == 0) {
            const char* name = libevdev_get_name(dev);
            // Look for the Touch Bar touch device
            if (name && (strstr(name, "Touch Bar") || strstr(name, "touch bar") ||
                         strstr(name, "Touchpad"))) {
                // Verify it has multitouch axes
                if (libevdev_has_event_code(dev, EV_ABS, ABS_MT_POSITION_X)) {
                    fd_ = fd;
                    dev_ = dev;
                    closedir(dir);
                    return true;
                }
            }
            libevdev_free(dev);
        }
        close(fd);
    }
    closedir(dir);
    return false;
}

int TouchInput::mapX(int raw) {
    // Map evdev X range to logical pixel X
    int width = layout_.width();
    if (abs_x_max_ <= abs_x_min_) return 0;
    int mapped = (raw - abs_x_min_) * width / (abs_x_max_ - abs_x_min_);
    if (mapped < 0) mapped = 0;
    if (mapped >= width) mapped = width - 1;
    return mapped;
}

int TouchInput::mapY(int raw) {
    // Map evdev Y range to logical pixel Y
    int height = layout_.height();
    if (abs_y_max_ <= abs_y_min_) return 0;
    int mapped = (raw - abs_y_min_) * height / (abs_y_max_ - abs_y_min_);
    if (mapped < 0) mapped = 0;
    if (mapped >= height) mapped = height - 1;
    return mapped;
}

void TouchInput::handleEvents() {
    struct input_event ev;
    int rc;

    while ((rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ev)) == 0 ||
           rc == LIBEVDEV_READ_STATUS_SYNC) {

        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // Re-sync after dropped events
            while (libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_SYNC, &ev) == LIBEVDEV_READ_STATUS_SYNC) {}
            continue;
        }

        if (ev.type == EV_ABS) {
            onAbsEvent(ev.code, ev.value);
        } else if (ev.type == EV_KEY) {
            onKeyEvent(ev.code, ev.value);
        } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            onSynReport();
        }
    }
}

void TouchInput::onAbsEvent(int code, int raw_value) {
    if (code == ABS_MT_POSITION_X || code == ABS_X) {
        // Cache every X report; evdev only omits the axis when its
        // value is unchanged, so this is always the true position.
        last_x_ = raw_value;
        cur_x_ = mapX(raw_value);
    } else if (code == ABS_MT_TRACKING_ID) {
        if (raw_value >= 0) pending_down_ = true;
        else pending_up_ = true;
    }
    // ABS_MT_POSITION_Y: never emitted by this driver — ignored.
    // ABS_MT_SLOT: slots wander; single-finger tracking ignores them.
}

void TouchInput::onKeyEvent(int code, int value) {
    if (code != BTN_TOUCH) return;
    if (value == 1) pending_down_ = true;
    else if (value == 0) pending_up_ = true;
}

void TouchInput::onSynReport() {
    if (pending_down_) {
        pending_down_ = false;
        if (state_ == State::Idle) {
            state_ = State::Down;
            // The press position is the cached axis value: if the down
            // frame carried an X event, cur_x_ already reflects it; if
            // not (same-X tap), last_x_ is correct by the evdev
            // invariant. Y is never reported — use mid-bar.
            cur_x_ = mapX(last_x_);
            cur_y_ = layout_.height() / 2;
            recordStart(cur_x_, cur_y_);
        }
    }
    if (pending_up_) {
        pending_up_ = false;
        onFingerUp();
    } else if (state_ == State::Down) {
        onMotion();
    }
}

void TouchInput::recordStart(int x, int y) {
    start_x_ = x;
    start_y_ = y;
    touch_start_ = std::chrono::steady_clock::now();

    // Remember where the press started: a gesture that begins on a
    // drag-capable spot (volume slider, seek strip) becomes a drag.
    down_widget_ = layout_.widgetAt(x);
    down_local_x_ = x;
    if (down_widget_) {
        for (auto& slot : layout_.slots()) {
            if (slot.widget == down_widget_) {
                down_local_x_ = x - slot.x_offset;
                break;
            }
        }
    }
    down_drag_capable_ =
        down_widget_ && down_widget_->dragCapableAt(down_local_x_, y);
    dragging_ = false;
    gesture_fired_ = false;
    if (longpress_fd_ >= 0)
        timerfd_set_ms(longpress_fd_, LONG_PRESS_MS, false);
    log_debug("Touch down at (%d, %d)%s", x, y,
              down_drag_capable_ ? " [drag-capable]" : "");
}

void TouchInput::longPressCheck() {
    if (state_ != State::Down || dragging_ || gesture_fired_ ||
        !down_widget_)
        return;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - touch_start_).count();
    if (ms < LONG_PRESS_MS) return;
    int dx = cur_x_ - start_x_;
    int dy = cur_y_ - start_y_;
    if (dx * dx + dy * dy >= TAP_MAX_DIST * TAP_MAX_DIST) return;

    gesture_fired_ = true;  // the release must not become a tap
    log_debug("Long press at (%d, %d)", start_x_, start_y_);
    down_widget_->onLongPress(down_local_x_, start_y_);
}

void TouchInput::onMotion() {
    if (state_ != State::Down || gesture_fired_) return;

    int dx = cur_x_ - start_x_;
    int dy = cur_y_ - start_y_;

    if (!dragging_) {
        GestureDecision d = classifyGesture(dx, dy, down_drag_capable_);
        if (d.gesture != Gesture::Drag)
            return;  // undecided, or a consumed long horizontal move
        dragging_ = true;
        down_widget_->onDrag(down_local_x_, start_y_, DragPhase::Begin);
    }

    // Stream widget-local coordinates.
    int local_x = down_local_x_ + (cur_x_ - start_x_);
    down_widget_->onDrag(local_x, cur_y_, DragPhase::Move);
}

void TouchInput::onFingerUp() {
    if (state_ == State::Idle) return;
    state_ = State::Idle;
    if (longpress_fd_ >= 0)
        timerfd_set_ms(longpress_fd_, 0, false);  // disarm

    int x = cur_x_;
    int y = cur_y_;

    if (dragging_) {
        int local_x = down_local_x_ + (x - start_x_);
        down_widget_->onDrag(local_x, y, DragPhase::End);
        return;
    }
    if (gesture_fired_) return;

    auto elapsed = std::chrono::steady_clock::now() - touch_start_;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    int dx = x - start_x_;
    int dy = y - start_y_;
    int dist = dx * dx + dy * dy;

    // Anything that moved too far is a consumed gesture, not a tap
    // (long horizontal moves used to be mode-cycle swipes; that
    // gesture was removed, but it still must not become a tap).
    if (dist < TAP_MAX_DIST * TAP_MAX_DIST && ms < TAP_MAX_MS) {
        // It's a tap — dispatch to the widget at tap position
        log_debug("Tap at (%d, %d)", x, y);
        Widget* w = layout_.widgetAt(x);
        if (w) {
            // Convert to widget-local coordinates
            int local_x = x;
            for (auto& slot : layout_.slots()) {
                if (slot.widget == w) {
                    local_x = x - slot.x_offset;
                    break;
                }
            }
            w->onTap(local_x, y);
        }
    }
}
