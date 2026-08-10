#pragma once
#include "widget/slider_widget.hpp"
#include <atomic>
#include <cstdint>
#include <pulse/pulseaudio.h>

// Right zone of the media center: a draggable volume slider with a
// mute pill on its left edge. Event-driven: a PulseAudio sink
// subscription tracks external changes (keyboard volume keys) and
// briefly pulses the zone highlight; drags/taps set the volume
// directly and are non-blocking (local state updates immediately).
// Track/knob/drag UX comes from SliderWidget.
class VolumeSliderWidget : public SliderWidget {
public:
    explicit VolumeSliderWidget(int max_percent = 100, int width = 300);
    ~VolumeSliderWidget() override;

    bool init() override;
    void shutdown() override;
    void update() override;
    void render(Framebuffer& fb) override;
    void onTap(int x, int y) override;
    int timerIntervalMs() const override { return 200; }  // pulse decay

    int volumePercent() const { return percent_; }
    bool muted() const { return muted_; }

    static constexpr int MUTE_PILL_W = ADORN_W;  // tap zone: mute toggle

private:
    void applyPercent(int percent) override;  // -> PulseAudio
    uint32_t fillColor() const override;
    uint32_t knobColor() const override;
    uint32_t textColor() const override;

    void fetchVolume();
    void toggleMute();
    void pulse();  // external-change highlight (PA thread)
    bool pulsing() const;
    static int64_t nowMs();

    static void sink_info_cb(pa_context* c, const pa_sink_info* info,
                             int eol, void* userdata);
    static void context_state_cb(pa_context* c, void* userdata);
    static void subscribe_cb(pa_context* c, pa_subscription_event_type_t t,
                             uint32_t idx, void* userdata);
    static void success_cb(pa_context* c, int success, void* userdata);

    pa_threaded_mainloop* pa_loop_ = nullptr;
    pa_context* pa_ctx_ = nullptr;
    bool muted_ = false;
    bool connected_ = false;

    // Written from the PA mainloop thread, read on the UI thread.
    std::atomic<int64_t> pulse_until_ms_{0};
    // Suppresses the pulse for changes we caused ourselves.
    std::atomic<int64_t> suppress_until_ms_{0};
};
