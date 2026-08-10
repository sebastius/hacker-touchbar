#pragma once

class Framebuffer;

// Render target for the composite framebuffer. Implementations:
// DrmOutput (touch bar hardware), DumpOutput (BMP frame dump,
// headless), SdlOutput (on-screen window, TOUCHBAR_FX_SCREEN).
class Output {
public:
    virtual ~Output() = default;
    virtual void present(const Framebuffer& fb) = 0;
};
