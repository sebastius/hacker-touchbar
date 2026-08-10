#pragma once
#include "audio/audio_source.hpp"
#include <pulse/simple.h>
#include <pulse/pulseaudio.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Records all system audio from the default sink's PipeWire/PulseAudio
// monitor source ("<sink>.monitor"). Falls back to the default source
// when no monitor can be found. Mixes down to mono into a ring buffer.
//
// The capture thread reconnects indefinitely: if the server goes away
// (PipeWire restart, ...) or the initial connect fails, the whole
// connect (default-sink lookup -> .monitor -> pa_simple_new) is
// retried every ~2 s. read() simply sees silence while disconnected.
class PulseMonitorSource : public AudioSource {
public:
    PulseMonitorSource();
    ~PulseMonitorSource();

    bool start() override;
    void stop() override;
    int sampleRate() const override { return SAMPLE_RATE; }
    size_t read(float* out, size_t max_samples) override;

    // Test hook: number of connect attempts made so far.
    int connectAttempts() const { return connect_attempts_.load(); }

private:
    // Query the server for the default sink name via the async API.
    std::string findDefaultSink();
    void captureLoop();
    void push(const float* samples, size_t count);

    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNELS = 2;
    static constexpr size_t RING_SIZE = SAMPLE_RATE * 2;  // ~2 seconds
    static constexpr int RETRY_MS = 2000;
    static constexpr int64_t LOG_EVERY_MS = 30000;

    std::thread capture_thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> connect_attempts_{0};

    std::mutex ring_mutex_;
    std::vector<float> ring_;
    size_t ring_head_ = 0;  // next read position
    size_t ring_tail_ = 0;  // next write position
};
