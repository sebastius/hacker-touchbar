#pragma once
#include "audio/audio_source.hpp"
#include <cstdint>

// Synthetic audio for headless testing: mixed sines with slowly
// shifting amplitudes plus a synthetic kick every ~500 ms, so frame
// dumps show real visualizer motion (moving bars + beat flashes).
class NullAudioSource : public AudioSource {
public:
    bool start() override { return true; }
    void stop() override {}
    int sampleRate() const override { return SAMPLE_RATE; }
    size_t read(float* out, size_t max_samples) override;

    static constexpr int SAMPLE_RATE = 48000;

private:
    uint64_t pos_ = 0;  // absolute sample position
};
