#pragma once
#include <functional>
#include <string>

class EventLoop;

// Minimal HTTP bridge to the IPC command handler, bound to
// 127.0.0.1 only. Exists for clients that cannot open the unix
// socket — browser-sandboxed scripts (CEF extensions can fetch() but
// not spawn processes), shell one-liners, and the like. Album art and
// hues normally arrive via the MPRIS art fetcher instead; this is
// the manual/external override path.
//
// Protocol: GET /<command with / as word separator>, e.g.
//   GET /hue/190/12      -> handler_("hue 190 12")
//   GET /preset/matrix   -> handler_("preset matrix")
// Reply: 200 text/plain with the handler's answer.
class HttpBridge {
public:
    explicit HttpBridge(EventLoop& loop) : loop_(loop) {}
    ~HttpBridge();

    void setHandler(std::function<std::string(const std::string&)> h) {
        handler_ = std::move(h);
    }
    bool init(int port = 8323);

private:
    void onEvent();

    EventLoop& loop_;
    std::function<std::string(const std::string&)> handler_;
    int fd_ = -1;
};
