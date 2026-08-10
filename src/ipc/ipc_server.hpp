#pragma once
#include <functional>
#include <string>

class EventLoop;

// Unix-socket IPC server at $XDG_RUNTIME_DIR/touchbar-fx.sock.
// Accepts one-line commands ("cycle", "mode visualizer|media|sliders")
// so a Hyprland keybind (via touchbar-fxctl) can drive the daemon.
class IpcServer {
public:
    explicit IpcServer(EventLoop& loop);
    ~IpcServer();

    bool init();
    void setHandler(std::function<std::string(const std::string&)> handler);

    const std::string& path() const { return path_; }

    // $XDG_RUNTIME_DIR/touchbar-fx.sock, falling back to
    // /tmp/touchbar-fx-<uid>.sock.
    static std::string defaultPath();

private:
    void onEvent();

    EventLoop& loop_;
    int fd_ = -1;
    std::string path_;
    std::function<std::string(const std::string&)> handler_;
};
