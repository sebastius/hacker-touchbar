#pragma once
#include "media/art_analyze.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <string>

class EventLoop;

// Downloads and analyzes album art for the current MPRIS track.
// Browsers and players only hand out a URL (mpris:artUrl), never
// pixels, so a worker thread runs `curl | magick` to turn it into a
// 28x28 RGB image, analyzes it there, and posts the result back to
// the event loop through an eventfd — albumArt() and the theme stay
// strictly main-thread. Repeat URLs are deduped; a fetch in flight
// coalesces newer requests (only the latest URL matters).
class ArtFetcher {
public:
    static constexpr int kArtSize = 28;

    explicit ArtFetcher(EventLoop& loop);
    ~ArtFetcher();

    void setCallback(std::function<void(const ArtAnalysis&)> cb) {
        callback_ = std::move(cb);
    }

    // Kick a fetch. No-op for empty or already-fetched URLs.
    void fetch(const std::string& url);

private:
    struct State {
        ~State();
        std::mutex mu;
        std::string pending_url;    // latest request, worker drains it
        bool worker_running = false;
        bool cancelled = false;     // set by ~ArtFetcher
        std::unique_ptr<ArtAnalysis> result;  // worker -> main handoff
        int efd = -1;               // shared wakeup channel
    };

    static void workerMain(const std::shared_ptr<State>& st);
    void onEvent(const std::shared_ptr<State>& st);

    EventLoop& loop_;
    std::shared_ptr<State> st_;     // shared with the detached worker
    std::string last_url_;          // main thread only
    std::function<void(const ArtAnalysis&)> callback_;
};
