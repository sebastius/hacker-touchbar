#include "audio/pulse_monitor_source.hpp"
#include "util/log.hpp"
#include <pulse/error.h>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

PulseMonitorSource::PulseMonitorSource() {
    ring_.resize(RING_SIZE, 0.0f);
}

PulseMonitorSource::~PulseMonitorSource() {
    stop();
}

// Resolve the server string the same way the volume widget does:
// honor PULSE_SERVER, and when running as root via sudo, reach the
// invoking user's PulseAudio/PipeWire socket.
static const char* resolve_server(char* buf, size_t buf_size) {
    const char* server = getenv("PULSE_SERVER");
    if (!server && getuid() == 0) {
        const char* sudo_uid = getenv("SUDO_UID");
        if (sudo_uid) {
            snprintf(buf, buf_size, "unix:/run/user/%s/pulse/native", sudo_uid);
            server = buf;
        }
    }
    return server;
}

std::string PulseMonitorSource::findDefaultSink() {
    std::string sink;

    pa_threaded_mainloop* loop = pa_threaded_mainloop_new();
    if (!loop) return sink;
    pa_mainloop_api* api = pa_threaded_mainloop_get_api(loop);
    pa_context* ctx = pa_context_new(api, "touchbar-fx");
    if (!ctx) {
        pa_threaded_mainloop_free(loop);
        return sink;
    }

    struct CbData {
        pa_threaded_mainloop* loop;
        pa_context* ctx;
        std::string* sink;
        bool ready = false;
        bool failed = false;
    } data{loop, ctx, &sink, false, false};

    pa_context_set_state_callback(ctx, [](pa_context* c, void* userdata) {
        auto* d = static_cast<CbData*>(userdata);
        pa_context_state_t state = pa_context_get_state(c);
        if (state == PA_CONTEXT_READY) d->ready = true;
        else if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED) d->failed = true;
        pa_threaded_mainloop_signal(d->loop, 0);
    }, &data);

    char server_buf[128] = {};
    const char* server = resolve_server(server_buf, sizeof(server_buf));

    pa_threaded_mainloop_lock(loop);
    pa_threaded_mainloop_start(loop);
    pa_context_connect(ctx, server, PA_CONTEXT_NOFLAGS, nullptr);
    while (!data.ready && !data.failed)
        pa_threaded_mainloop_wait(loop);

    if (data.ready) {
        pa_operation* op = pa_context_get_server_info(ctx,
            [](pa_context*, const pa_server_info* info, void* userdata) {
                auto* d = static_cast<CbData*>(userdata);
                if (info && info->default_sink_name)
                    *d->sink = info->default_sink_name;
                pa_threaded_mainloop_signal(d->loop, 0);
            }, &data);
        if (op) {
            while (pa_operation_get_state(op) == PA_OPERATION_RUNNING)
                pa_threaded_mainloop_wait(loop);
            pa_operation_unref(op);
        }
    }

    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_threaded_mainloop_unlock(loop);
    pa_threaded_mainloop_stop(loop);
    pa_threaded_mainloop_free(loop);
    return sink;
}

bool PulseMonitorSource::start() {
    running_ = true;
    capture_thread_ = std::thread(&PulseMonitorSource::captureLoop, this);
    return true;
}

void PulseMonitorSource::stop() {
    running_ = false;
    if (capture_thread_.joinable())
        capture_thread_.join();
}

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void PulseMonitorSource::captureLoop() {
    pa_sample_spec spec;
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = SAMPLE_RATE;
    spec.channels = CHANNELS;

    pa_buffer_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.maxlength = static_cast<uint32_t>(-1);
    attr.fragsize = 1024 * CHANNELS * sizeof(int16_t);

    char server_buf[128] = {};
    const char* server = resolve_server(server_buf, sizeof(server_buf));

    int64_t last_log_ms = 0;
    bool was_connected = false;
    std::vector<int16_t> buffer(1024 * CHANNELS);
    std::vector<float> mono(1024);

    // Reconnect forever: server restarts (PipeWire upgrades!) drop the
    // stream, and the daemon must recover without a restart.
    while (running_) {
        ++connect_attempts_;
        std::string sink = findDefaultSink();
        std::string device = sink.empty() ? "" : sink + ".monitor";

        int error;
        pa_simple* pa = pa_simple_new(server, "touchbar-fx", PA_STREAM_RECORD,
                                      device.empty() ? nullptr : device.c_str(),
                                      "Visualizer", &spec, nullptr, &attr,
                                      &error);
        if (!pa) {
            int64_t now = now_ms();
            if (now - last_log_ms >= LOG_EVERY_MS) {
                log_warn("Audio: connect failed: %s — retrying every %ds",
                         pa_strerror(error), RETRY_MS / 1000);
                last_log_ms = now;
            }
        } else {
            log_info("Audio: %scapturing system audio from %s",
                     was_connected ? "reconnected, " : "",
                     device.empty() ? "default source" : device.c_str());
            was_connected = true;
            last_log_ms = 0;

            while (running_) {
                if (pa_simple_read(pa, buffer.data(),
                                   buffer.size() * sizeof(int16_t),
                                   &error) < 0) {
                    log_warn("Audio: read failed: %s — reconnecting",
                             pa_strerror(error));
                    break;
                }
                // Mix down to mono
                for (size_t i = 0; i < 1024; ++i)
                    mono[i] = (buffer[i * 2] + buffer[i * 2 + 1]) /
                              (2.0f * 32768.0f);
                push(mono.data(), mono.size());
            }
            pa_simple_free(pa);
        }

        // Back off ~2 s between attempts, responsive to stop().
        for (int i = 0; i < RETRY_MS / 50 && running_; ++i)
            usleep(50'000);
    }
}

void PulseMonitorSource::push(const float* samples, size_t count) {
    std::lock_guard<std::mutex> lock(ring_mutex_);
    for (size_t i = 0; i < count; ++i) {
        ring_[ring_tail_] = samples[i];
        ring_tail_ = (ring_tail_ + 1) % RING_SIZE;
        if (ring_tail_ == ring_head_)  // overrun: drop oldest
            ring_head_ = (ring_head_ + 1) % RING_SIZE;
    }
}

size_t PulseMonitorSource::read(float* out, size_t max_samples) {
    std::lock_guard<std::mutex> lock(ring_mutex_);
    size_t n = 0;
    while (n < max_samples && ring_head_ != ring_tail_) {
        out[n++] = ring_[ring_head_];
        ring_head_ = (ring_head_ + 1) % RING_SIZE;
    }
    return n;
}
