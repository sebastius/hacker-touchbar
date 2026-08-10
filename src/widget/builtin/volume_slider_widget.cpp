#include "widget/builtin/volume_slider_widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include "util/log.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {
constexpr int64_t PULSE_MS = 1200;      // external-change highlight
constexpr int64_t SELF_SUPPRESS_MS = 600;
}

VolumeSliderWidget::VolumeSliderWidget(int max_percent, int width)
    : SliderWidget(max_percent, width)
{
    name_ = "volume-slider";
}

VolumeSliderWidget::~VolumeSliderWidget() { shutdown(); }

int64_t VolumeSliderWidget::nowMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ---- PulseAudio ----

void VolumeSliderWidget::context_state_cb(pa_context* c, void* userdata) {
    auto* self = static_cast<VolumeSliderWidget*>(userdata);
    pa_context_state_t state = pa_context_get_state(c);
    if (state == PA_CONTEXT_READY) {
        self->connected_ = true;
        // Event-driven sink tracking: external volume changes (media
        // keys, pavucontrol, ...) arrive here instead of a 1 Hz poll.
        pa_context_set_subscribe_callback(c, subscribe_cb, self);
        pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, success_cb, self);
    } else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
        self->connected_ = false;
    }
    pa_threaded_mainloop_signal(self->pa_loop_, 0);
}

void VolumeSliderWidget::subscribe_cb(pa_context* c,
                                      pa_subscription_event_type_t t,
                                      uint32_t, void* userdata) {
    auto* self = static_cast<VolumeSliderWidget*>(userdata);
    if ((t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) ==
        PA_SUBSCRIPTION_EVENT_SINK) {
        pa_operation* op = pa_context_get_sink_info_by_name(
            c, "@DEFAULT_SINK@", sink_info_cb, self);
        if (op) pa_operation_unref(op);  // async; no wait from the callback
    }
}

void VolumeSliderWidget::sink_info_cb(pa_context*, const pa_sink_info* info,
                                      int eol, void* userdata) {
    auto* self = static_cast<VolumeSliderWidget*>(userdata);
    if (eol > 0 || !info) {
        pa_threaded_mainloop_signal(self->pa_loop_, 0);
        return;
    }
    int new_vol = static_cast<int>(pa_cvolume_avg(&info->volume) * 100 /
                                   PA_VOLUME_NORM);
    bool new_mute = info->mute != 0;
    if (new_vol != self->percent_ || new_mute != self->muted_) {
        bool self_caused = nowMs() < self->suppress_until_ms_.load();
        self->setPercentFromBackend(new_vol);
        self->muted_ = new_mute;
        if (!self_caused) self->pulse();
        self->markDirty();
    }
    pa_threaded_mainloop_signal(self->pa_loop_, 0);
}

void VolumeSliderWidget::success_cb(pa_context*, int, void* userdata) {
    auto* self = static_cast<VolumeSliderWidget*>(userdata);
    pa_threaded_mainloop_signal(self->pa_loop_, 0);
}

bool VolumeSliderWidget::init() {
    pa_loop_ = pa_threaded_mainloop_new();
    if (!pa_loop_) return false;
    pa_mainloop_api* api = pa_threaded_mainloop_get_api(pa_loop_);
    pa_ctx_ = pa_context_new(api, "touchbar-fx");
    if (!pa_ctx_) {
        pa_threaded_mainloop_free(pa_loop_);
        pa_loop_ = nullptr;
        return false;
    }
    pa_context_set_state_callback(pa_ctx_, context_state_cb, this);

    // Handle sudo: connect to the invoking user's PulseAudio
    const char* server = getenv("PULSE_SERVER");
    char server_buf[128] = {};
    if (!server && getuid() == 0) {
        const char* sudo_uid = getenv("SUDO_UID");
        if (sudo_uid) {
            snprintf(server_buf, sizeof(server_buf),
                     "unix:/run/user/%s/pulse/native", sudo_uid);
            server = server_buf;
        }
    }

    pa_threaded_mainloop_lock(pa_loop_);
    pa_threaded_mainloop_start(pa_loop_);
    pa_context_connect(pa_ctx_, server, PA_CONTEXT_NOFLAGS, nullptr);
    while (!connected_) {
        pa_context_state_t state = pa_context_get_state(pa_ctx_);
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) {
            pa_context_disconnect(pa_ctx_);
            pa_context_unref(pa_ctx_);
            pa_ctx_ = nullptr;
            pa_threaded_mainloop_unlock(pa_loop_);
            pa_threaded_mainloop_stop(pa_loop_);
            pa_threaded_mainloop_free(pa_loop_);
            pa_loop_ = nullptr;
            log_warn("Volume slider: PulseAudio connection failed");
            return false;
        }
        pa_threaded_mainloop_wait(pa_loop_);
    }
    pa_threaded_mainloop_unlock(pa_loop_);
    log_info("Volume slider connected (max %d%%)", max_percent_);
    fetchVolume();
    return true;
}

void VolumeSliderWidget::shutdown() {
    if (pa_ctx_) {
        pa_threaded_mainloop_lock(pa_loop_);
        pa_context_disconnect(pa_ctx_);
        pa_context_unref(pa_ctx_);
        pa_ctx_ = nullptr;
        pa_threaded_mainloop_unlock(pa_loop_);
    }
    if (pa_loop_) {
        pa_threaded_mainloop_stop(pa_loop_);
        pa_threaded_mainloop_free(pa_loop_);
        pa_loop_ = nullptr;
    }
    connected_ = false;
}

void VolumeSliderWidget::fetchVolume() {
    if (!connected_) return;
    pa_threaded_mainloop_lock(pa_loop_);
    pa_operation* op = pa_context_get_sink_info_by_name(
        pa_ctx_, "@DEFAULT_SINK@", sink_info_cb, this);
    if (op) {
        while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
            pa_threaded_mainloop_wait(pa_loop_);
        pa_operation_unref(op);
    }
    pa_threaded_mainloop_unlock(pa_loop_);
}

void VolumeSliderWidget::applyPercent(int percent) {
    suppress_until_ms_ = nowMs() + SELF_SUPPRESS_MS;
    if (!connected_) return;

    pa_cvolume vol;
    pa_cvolume_init(&vol);
    pa_cvolume_set(&vol, 2, percent * PA_VOLUME_NORM / 100);
    pa_threaded_mainloop_lock(pa_loop_);
    pa_operation* op = pa_context_set_sink_volume_by_name(
        pa_ctx_, "@DEFAULT_SINK@", &vol, nullptr, nullptr);
    if (op) pa_operation_unref(op);  // fire and forget: drags stream these
    pa_threaded_mainloop_unlock(pa_loop_);
}

void VolumeSliderWidget::toggleMute() {
    suppress_until_ms_ = nowMs() + SELF_SUPPRESS_MS;
    muted_ = !muted_;
    markDirty();
    if (!connected_) return;
    pa_threaded_mainloop_lock(pa_loop_);
    pa_operation* op = pa_context_set_sink_mute_by_name(
        pa_ctx_, "@DEFAULT_SINK@", muted_, nullptr, nullptr);
    if (op) pa_operation_unref(op);
    pa_threaded_mainloop_unlock(pa_loop_);
}

// ---- pulse highlight ----

void VolumeSliderWidget::pulse() {
    pulse_until_ms_ = nowMs() + PULSE_MS;
}

bool VolumeSliderWidget::pulsing() const {
    return nowMs() < pulse_until_ms_.load();
}

void VolumeSliderWidget::update() {
    // Expire the external-change pulse; volume data itself is
    // subscription-driven.
    int64_t until = pulse_until_ms_.load();
    if (until > 0 && nowMs() >= until) {
        pulse_until_ms_ = 0;
        markDirty();
    }
}

// ---- SliderWidget theme hooks ----

uint32_t VolumeSliderWidget::fillColor() const {
    if (muted_) return theme::red;
    return pulsing() ? theme::green : theme::green_dim;
}

uint32_t VolumeSliderWidget::knobColor() const {
    return pulsing() ? theme::accent : theme::green;
}

uint32_t VolumeSliderWidget::textColor() const {
    return pulsing() ? theme::text : theme::text_dim;
}

// ---- input ----

void VolumeSliderWidget::onTap(int x, int y) {
    if (x < MUTE_PILL_W)
        toggleMute();
    else
        SliderWidget::onTap(x, y);
}

// ---- render ----

void VolumeSliderWidget::render(Framebuffer& fb) {
    fb.clear(theme::bg);

    int w = fb.width();
    int h = fb.height();
    bool hot = pulsing();

    if (hot) {
        // External volume change: pulse the whole zone.
        fb.fillRoundRect(2, 2, w - 4, h - 4, theme::btn_radius, theme::green_bg);
        fb.drawRoundRect(2, 2, w - 4, h - 4, theme::btn_radius, theme::green);
    }

    // Mute pill with speaker glyph
    int btn_y = 2, btn_h = h - 4;
    fb.fillRoundRect(6, btn_y, MUTE_PILL_W - 12, btn_h, theme::btn_radius,
                     theme::btn);
    fb.drawRoundRect(6, btn_y, MUTE_PILL_W - 12, btn_h, theme::btn_radius,
                     theme::btn_border);
    uint32_t glyph = muted_ ? theme::red : theme::green;
    int gx = 14, gy = h / 2;
    fb.fillRect(gx, gy - 3, 4, 6, glyph);                       // speaker box
    for (int row = 0; row < 12; ++row) {                        // cone
        int lw = (row <= 6) ? row / 2 + 1 : (12 - row) / 2 + 1;
        fb.drawHLine(gx + 4, gy - 6 + row, lw, glyph);
    }
    if (muted_) {
        fb.drawLine(gx + 10, gy - 6, gx + 18, gy + 6, theme::red);
        fb.drawLine(gx + 18, gy - 6, gx + 10, gy + 6, theme::red);
    } else {
        fb.drawVLine(gx + 11, gy - 4, 9, glyph);                // sound waves
        fb.drawVLine(gx + 14, gy - 6, 13, glyph);
    }

    drawSlider(fb);
}
