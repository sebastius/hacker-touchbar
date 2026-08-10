#include "media/art_fetcher.hpp"
#include "engine/event_loop.hpp"
#include "util/log.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

ArtFetcher::State::~State() {
    if (efd >= 0) close(efd);
}

ArtFetcher::ArtFetcher(EventLoop& loop)
    : loop_(loop), st_(std::make_shared<State>()) {
    st_->efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (st_->efd < 0) {
        log_warn("ArtFetcher: eventfd failed, art fetch disabled");
        st_->cancelled = true;
        return;
    }
    auto st = st_;
    loop_.addFd(st_->efd, EPOLLIN, [this, st](uint32_t) { onEvent(st); });
}

ArtFetcher::~ArtFetcher() {
    {
        std::lock_guard<std::mutex> lk(st_->mu);
        st_->cancelled = true;
    }
    if (st_->efd >= 0) {
        loop_.removeFd(st_->efd);
        st_->efd = -1;  // ~State closes it once the worker is done too
    }
    // The worker (if any) is detached and holds its own shared_ptr; it
    // sees cancelled and exits without touching the eventfd.
}

void ArtFetcher::fetch(const std::string& url) {
    if (url.empty() || url == last_url_) return;
    last_url_ = url;
    {
        std::lock_guard<std::mutex> lk(st_->mu);
        if (st_->cancelled) return;
        st_->pending_url = url;
        if (st_->worker_running) return;  // coalesced: worker picks it up
        st_->worker_running = true;
    }
    std::thread([st = st_] { workerMain(st); }).detach();
}

void ArtFetcher::workerMain(const std::shared_ptr<State>& st) {
    for (;;) {
        std::string url;
        {
            std::lock_guard<std::mutex> lk(st->mu);
            if (st->cancelled || st->pending_url.empty()) {
                st->worker_running = false;
                return;
            }
            url = std::move(st->pending_url);
            st->pending_url.clear();
        }

        // curl downloads once (http(s) and file:// both work); magick
        // decodes jpg/png/webp and emits TWO 28x28 raw RGB squares:
        // first the center-crop for the display thumbnail (wide
        // covers lose their sides instead of being squeezed), then a
        // squeezed full-frame copy for the color analysis — cropping
        // for hues would throw away color blocks that sit outside
        // the center (collage covers).
        std::string quoted = "'";
        for (char c : url) quoted += (c == '\'') ? "'\\''" : std::string(1, c);
        quoted += "'";
        char cmd[4096];
        snprintf(cmd, sizeof(cmd),
                 "f=$(mktemp /tmp/tbfx-art-XXXXXX) && "
                 "curl -sL --max-time 8 -o \"$f\" -- %s 2>/dev/null && "
                 "{ magick \"$f\" -resize %dx%d^ -gravity center "
                 "-extent %dx%d -depth 8 rgb:- && "
                 "magick \"$f\" -resize %dx%d! -depth 8 rgb:- ; } ; "
                 "rm -f \"$f\"",
                 quoted.c_str(), kArtSize, kArtSize, kArtSize, kArtSize,
                 kArtSize, kArtSize);

        auto result = std::make_unique<ArtAnalysis>();
        bool ok = false;
        if (FILE* p = popen(cmd, "r")) {
            const size_t want = kArtSize * kArtSize * 3;
            std::vector<uint8_t> buf(want * 2);
            size_t got = 0;
            while (got < buf.size()) {
                size_t n = fread(buf.data() + got, 1, buf.size() - got, p);
                if (n == 0) break;
                got += n;
            }
            pclose(p);
            if (got == buf.size()) {
                // Hues from the full frame...
                *result = analyze_art(buf.data() + want, kArtSize, kArtSize);
                // ...but display pixels from the center crop.
                for (size_t i = 0; i < want / 3; ++i)
                    result->px[i] = 0xFF000000u | (buf[i * 3] << 16)
                                    | (buf[i * 3 + 1] << 8) | buf[i * 3 + 2];
                ok = true;
            }
        }

        std::lock_guard<std::mutex> lk(st->mu);
        if (st->cancelled) {
            st->worker_running = false;
            return;
        }
        if (ok) {
            st->result = std::move(result);
            if (st->efd >= 0) {
                uint64_t one = 1;
                if (write(st->efd, &one, sizeof(one)) < 0)
                    log_warn("ArtFetcher: eventfd write failed");
            }
        } else {
            log_warn("ArtFetcher: fetch/decode failed for %.60s...",
                     url.c_str());
        }
        // Loop around: a newer request may have queued while we worked.
    }
}

void ArtFetcher::onEvent(const std::shared_ptr<State>& st) {
    uint64_t tmp;
    while (read(st->efd, &tmp, sizeof(tmp)) == sizeof(tmp)) {}

    std::unique_ptr<ArtAnalysis> result;
    {
        std::lock_guard<std::mutex> lk(st->mu);
        result = std::move(st->result);
    }
    if (result && callback_) callback_(*result);
}
