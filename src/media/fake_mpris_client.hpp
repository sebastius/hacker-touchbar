#pragma once
#include "media/mpris_client.hpp"

// Deterministic MPRIS stand-in for the headless dump harness and the
// test binary: pretends a track is playing, advances position on
// poll(), records seek targets.
class FakeMprisClient : public MprisClient {
public:
    FakeMprisClient() {
        state_.has_player = true;
        state_.playing = true;
        state_.title = "Neon Skyline";
        state_.artist = "Midnight Circuit";
        state_.track_id = "/org/mpris/MediaPlayer2/track/fake/1";
        state_.length_us = 227'000'000;    // 3:47
        state_.position_us = 42'000'000;   // 0:42
    }

    bool init() override { return true; }

    void poll(int elapsed_ms) override {
        if (!state_.playing) return;
        state_.position_us += static_cast<int64_t>(elapsed_ms) * 1000;
        if (state_.position_us > state_.length_us)
            state_.position_us = state_.length_us;
    }

    const MprisState& state() const override { return state_; }

    void playPause() override { state_.playing = !state_.playing; }
    void next() override {}
    void previous() override {}
    void seekTo(int64_t position_us) override {
        state_.position_us = position_us;
        last_seek_us = position_us;
        ++seek_count;
    }

    // Test hooks
    int64_t last_seek_us = -1;
    int seek_count = 0;

private:
    MprisState state_;
};
