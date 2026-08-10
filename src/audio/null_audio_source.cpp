#include "audio/null_audio_source.hpp"
#include <cmath>

size_t NullAudioSource::read(float* out, size_t max_samples) {
    static constexpr double TWO_PI = 6.283185307179586;
    // Sustained partials; each wobbles at its own slow LFO so the
    // spectrum visibly shifts over time.
    static constexpr double FREQS[] = {110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0, 7040.0};
    static constexpr double AMPS[]  = {0.30, 0.22, 0.18, 0.14, 0.11, 0.08, 0.06};
    static constexpr double LFOS[]  = {0.70, 0.50, 0.90, 0.35, 1.10, 0.55, 0.80};
    static constexpr int N = 7;

    static constexpr int BEAT_PERIOD = SAMPLE_RATE / 2;  // every 500 ms
    static constexpr int BEAT_LEN = SAMPLE_RATE / 10;    // 100 ms thump

    for (size_t i = 0; i < max_samples; ++i) {
        uint64_t p = pos_++;
        double t = static_cast<double>(p) / SAMPLE_RATE;

        double s = 0.0;
        for (int k = 0; k < N; ++k) {
            double wobble = 0.6 + 0.4 * std::sin(TWO_PI * LFOS[k] * t + k * 1.7);
            s += AMPS[k] * wobble * std::sin(TWO_PI * FREQS[k] * t);
        }

        int beat_phase = static_cast<int>(p % BEAT_PERIOD);
        if (beat_phase < BEAT_LEN) {
            double env = std::exp(-5.0 * beat_phase / BEAT_LEN);
            s += 0.9 * env * std::sin(TWO_PI * 55.0 * t);
        }

        if (s > 1.0) s = 1.0;
        if (s < -1.0) s = -1.0;
        out[i] = static_cast<float>(s);
    }
    return max_samples;
}
