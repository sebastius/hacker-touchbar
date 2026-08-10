#pragma once
#include <cstdlib>

// Pure gesture classification, shared by TouchInput and the test
// binary. Thresholds (logical pixels, bar is 60 px tall):
//
//   TAP_MAX_DIST    20 px + TAP_MAX_MS 400 ms  -> tap (decided on finger-up)
//   DRAG_START_DIST 12 px horizontal-dominant, starting on a
//                   drag-capable widget         -> drag (streams onDrag)
//   SWIPE_MIN_DIST  120 px horizontal, 2x dominant over vertical
//                                               -> consumed (mode-cycle
//                                                  swipes were removed;
//                                                  it must not become a tap)
//
// There is NO vertical gesture: this digitizer never emits Y events,
// so vertical swipes are physically undetectable (presets cycle via
// the ✦ button / IPC instead).

enum class Gesture {
    Undecided,        // not enough movement yet (may still become a tap)
    Drag,             // stream finger moves to the start widget
    SwipeHorizontal,  // consumed; no action wired
};

struct GestureDecision {
    Gesture gesture;
    int dir;
};

inline GestureDecision classifyGesture(int dx, int dy, bool start_drag_capable) {
    constexpr int DRAG_START_DIST = 12;
    constexpr int SWIPE_MIN_DIST = 120;

    const int adx = std::abs(dx);
    const int ady = std::abs(dy);

    if (start_drag_capable && adx >= DRAG_START_DIST && adx >= ady)
        return {Gesture::Drag, 0};
    if (adx >= SWIPE_MIN_DIST && adx > 2 * ady)
        return {Gesture::SwipeHorizontal, dx > 0 ? 1 : -1};
    return {Gesture::Undecided, 0};
}
