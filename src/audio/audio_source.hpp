#pragma once
#include <cstddef>

// A source of mono audio samples in [-1, 1] for the visualizer.
class AudioSource {
public:
    virtual ~AudioSource() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;

    virtual int sampleRate() const = 0;

    // Copy up to max_samples of the most recent mono samples into out.
    // Returns the number of samples written (0 when none are available).
    virtual size_t read(float* out, size_t max_samples) = 0;
};
