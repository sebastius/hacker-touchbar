#pragma once
#include <cstdint>
#include <functional>
#include <string>

// What the media center shows/controls.
struct MprisState {
    bool has_player = false;
    bool playing = false;          // PlaybackStatus == "Playing"
    std::string title;
    std::string artist;
    std::string album;             // xesam:album
    std::string track_id;          // mpris:trackid (D-Bus object path)
    std::string art_url;           // mpris:artUrl (http(s) or file path)
    std::string player;            // well-known bus name, e.g. "...firefox"
    int64_t length_us = 0;         // mpris:length, microseconds
    int64_t position_us = 0;       // interpolated between signals
};

enum class MprisEvent {
    TrackChanged,      // new track on the active player (or new active track)
    PlaybackStarted,   // active player went paused/stopped -> playing
    StateChanged,      // anything else (pause, player swap, player gone)
};

// MPRIS source. The real implementation is event-driven
// (PropertiesChanged / NameOwnerChanged over D-Bus, watches hooked
// into the epoll loop); a fake implementation backs headless dumps
// and tests. Arbitration: the player with PlaybackStatus=Playing
// wins; ties and the no-playing case fall to the most recently
// active player.
class MprisClient {
public:
    virtual ~MprisClient() = default;

    virtual bool init() = 0;
    virtual void shutdown() {}

    // Lightweight tick (called from the visualizer's frame timer):
    // interpolates position while playing and resyncs it against the
    // player every couple of seconds. NOT a metadata poll.
    virtual void poll(int elapsed_ms) = 0;

    virtual const MprisState& state() const = 0;

    virtual void playPause() = 0;
    virtual void next() = 0;
    virtual void previous() = 0;
    virtual void seekTo(int64_t position_us) = 0;

    // Fired (main thread) when state worth redrawing/arbitrating changes.
    void setEventCallback(std::function<void(MprisEvent)> cb) {
        on_event_ = std::move(cb);
    }

protected:
    void fire(MprisEvent ev) { if (on_event_) on_event_(ev); }

    std::function<void(MprisEvent)> on_event_;
};
