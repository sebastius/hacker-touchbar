#pragma once

class Framebuffer;
class VizRenderer;

// A named visualizer look. The VizRenderer core owns FFT analysis,
// beat detection and the feedback-trail pass; a preset only draws the
// current frame on top of that. Add a preset by creating one file in
// src/viz/presets/ with a REGISTER_PRESET line at the bottom.
class VizPreset {
public:
    virtual ~VizPreset() = default;

    virtual const char* name() const = 0;

    // Draw onto fb (already filled with the decayed previous frame).
    virtual void draw(const VizRenderer& viz, Framebuffer& fb) = 0;

    // True when the preset fully overwrites fb every frame (clears
    // first) — the renderer then skips the feedback-decay pass and
    // the previous-frame copy, both pure waste for such presets.
    virtual bool clearsFrame() const { return false; }

    // Tap on the viz zone (coordinates in the same space draw() sees).
    // No-op by default; presets with interactive regions override it.
    virtual void onTap(int x, int y) { (void)x; (void)y; }

    // Long press on the viz zone. No-op by default.
    virtual void onLongPress(int x, int y) { (void)x; (void)y; }

    // Test/diagnostic hook: how many actors are currently airborne
    // (e.g. penguins mid-jump). 0 for presets without actors.
    virtual int airborneCount() const { return 0; }
};
