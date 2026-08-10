#include "ipc/ipc_server.hpp"
#include "engine/event_loop.hpp"
#include "util/log.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

IpcServer::IpcServer(EventLoop& loop) : loop_(loop) {}

IpcServer::~IpcServer() {
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
        close(fd_);
    }
    if (!path_.empty())
        unlink(path_.c_str());
}

std::string IpcServer::defaultPath() {
    const char* xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg)
        return std::string(xdg) + "/touchbar-fx.sock";
    // Running as root via sudo: use the invoking user's runtime dir so
    // touchbar-fxctl (running as that user) can find the socket.
    if (getuid() == 0) {
        if (const char* sudo_uid = getenv("SUDO_UID")) {
            std::string dir = std::string("/run/user/") + sudo_uid;
            if (access(dir.c_str(), F_OK) == 0)
                return dir + "/touchbar-fx.sock";
        }
    }
    return "/tmp/touchbar-fx-" + std::to_string(getuid()) + ".sock";
}

void IpcServer::setHandler(std::function<std::string(const std::string&)> handler) {
    handler_ = std::move(handler);
}

bool IpcServer::init() {
    path_ = defaultPath();

    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        log_warn("IPC: socket failed: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    if (path_.size() >= sizeof(addr.sun_path)) {
        log_warn("IPC: path too long: %s", path_.c_str());
        close(fd_);
        fd_ = -1;
        return false;
    }
    strcpy(addr.sun_path, path_.c_str());
    unlink(path_.c_str());  // stale socket from a previous run

    if (bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        log_warn("IPC: bind %s failed: %s", path_.c_str(), strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }
    if (listen(fd_, 4) < 0) {
        log_warn("IPC: listen failed: %s", strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Root under sudo: hand the socket to the invoking user so their
    // touchbar-fxctl can connect.
    if (getuid() == 0) {
        const char* sudo_uid = getenv("SUDO_UID");
        const char* sudo_gid = getenv("SUDO_GID");
        if (sudo_uid && sudo_gid)
            chown(path_.c_str(), atoi(sudo_uid), atoi(sudo_gid));
        chmod(path_.c_str(), 0660);
    }

    loop_.addFd(fd_, EPOLLIN, [this](uint32_t) { onEvent(); });
    log_info("IPC: listening on %s", path_.c_str());
    return true;
}

void IpcServer::onEvent() {
    int client = accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) return;

    char buf[512];
    ssize_t n = read(client, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string cmd(buf);
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' ||
                                cmd.back() == ' '))
            cmd.pop_back();
        std::string reply = handler_ ? handler_(cmd)
                                     : "err no handler";
        reply += '\n';
        ssize_t unused = write(client, reply.data(), reply.size());
        (void)unused;
    }
    close(client);
}
