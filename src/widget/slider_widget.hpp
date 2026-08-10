#pragma once
#include "widget/widget.hpp"
#include <cstdint>

// Shared thick-slider UX (volume + brightness): a 16 px track with a
// handle knob, the whole zone drag-capable at any height. Grabbing
// the track maps finger x directly to 0..max_percent; grabbing the
// dead zone left of the track follows the finger relatively, so the
// knob never jumps. Subclasses provide the backend (applyPercent),
// colors, and any left-side adornment (mute pill, sun icon, ...).
class SliderWidget : public Widget {
public:
    SliderWidget(int max_percent, int width);

    bool dragCapableAt(int, int) const override { return true; }
    void onTap(int x, int y) override;
    void onDrag(int x, int y, DragPhase phase) override;

    // x (widget-local) -> percent in [0, max_percent].
    int percentForX(int x) const;
    int percent() const { return percent_; }

    static constexpr int ADORN_W = 44;    // left icon/pill zone
    static constexpr int TRACK_X0 = 56;   // slider track left edge
    static constexpr int TRACK_PAD = 8;   // track right padding
    static constexpr int TRACK_H = 16;    // track thickness (bar is 60)

protected:
    // Backend hook: push the new value out (PA, sysfs, ...). Called
    // after percent_ has already been updated (optimistic UI).
    virtual void applyPercent(int percent) = 0;

    // Theme hooks.
    virtual uint32_t fillColor() const = 0;   // track fill
    virtual uint32_t knobColor() const = 0;   // knob dot
    virtual uint32_t textColor() const = 0;   // percent readout

    // Draws track + knob + percent readout; call from render() after
    // the subclass's adornments.
    void drawSlider(Framebuffer& fb);

    // Drag/tap path: clamp, update locally, notify backend, markDirty.
    void setPercentLocal(int percent);

    // Backend-driven path (PA subscription, sysfs re-read): no echo
    // back to the backend.
    void setPercentFromBackend(int percent);

    int max_percent_;
    int percent_ = 0;
    int grab_offset_ = 0;  // see onDrag
};
