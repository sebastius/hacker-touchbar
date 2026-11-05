#pragma once
#include <string>
#include <cstdint>

class Framebuffer;
class Scheduler;

// Drag gesture lifecycle delivered to drag-capable widgets.
enum class DragPhase { Begin, Move, End };

class Widget {
public:
    virtual ~Widget() = default;

    virtual bool init() { return true; }
    virtual void shutdown() {}

    // Called when this widget's timer fires (or data arrives).
    virtual void update() = 0;

    // Render into the provided sub-framebuffer (sized to allocated width x 60).
    virtual void render(Framebuffer& fb) = 0;

    // Touch input (coordinates local to this widget).
    virtual void onTap(int x, int y) { (void)x; (void)y; }

    // Raw finger down/up, before any tap or drag is decided. Release
    // always reaches the widget that got the press, even if the layout
    // changed under the finger, so real keys can't stick.
    virtual void onPress(int x, int y) { (void)x; (void)y; }
    virtual void onRelease() {}

    // Long-press (>= TouchInput's LONG_PRESS_MS with the finger held
    // within tap slop). Fires once while the finger is still down; the
    // subsequent release does NOT also fire a tap.
    virtual void onLongPress(int x, int y) { (void)x; (void)y; }

    // True when a press at this widget-local position can turn into a
    // drag (finger-move stream) instead of a swipe/tap. Position-dependent:
    // e.g. the visualizer only drags on its bottom seek strip.
    virtual bool dragCapableAt(int x, int y) const { (void)x; (void)y; return false; }

    // Finger-move stream while dragging, widget-local coordinates
    // (x may leave [0, width) when the finger exits the zone; clamp as
    // needed). Begin fires once when the gesture is classified as a
    // drag, Move on each motion event, End on finger-up.
    virtual void onDrag(int x, int y, DragPhase phase) {
        (void)x; (void)y; (void)phase;
    }

    // Timer interval in ms. 0 = no periodic timer.
    virtual int timerIntervalMs() const { return 0; }

    // Requested width in pixels. 0 = flexible (fill remaining space).
    int requestedWidth() const { return requested_width_; }
    void setAllocatedWidth(int w) { allocated_width_ = w; }
    int allocatedWidth() const { return allocated_width_; }

    const std::string& name() const { return name_; }

    bool isDirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

    void setScheduler(Scheduler* s) { scheduler_ = s; }

protected:
    void markDirty();

    std::string name_;
    int requested_width_ = 0;
    int allocated_width_ = 0;
    bool dirty_ = true;
    Scheduler* scheduler_ = nullptr;
};
