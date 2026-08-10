#include "ipc/http_bridge.hpp"
#include "engine/event_loop.hpp"
#include "util/log.hpp"
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

HttpBridge::~HttpBridge() {
    if (fd_ >= 0) {
        loop_.removeFd(fd_);
        close(fd_);
    }
}

bool HttpBridge::init(int port) {
    fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        log_warn("HTTP bridge: socket failed: %s", strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // local only
    if (bind(fd_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        log_warn("HTTP bridge: bind 127.0.0.1:%d failed: %s", port,
                 strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }
    if (listen(fd_, 4) < 0) {
        log_warn("HTTP bridge: listen failed: %s", strerror(errno));
        close(fd_);
        fd_ = -1;
        return false;
    }
    loop_.addFd(fd_, EPOLLIN, [this](uint32_t) { onEvent(); });
    log_info("HTTP bridge: listening on 127.0.0.1:%d", port);
    return true;
}

void HttpBridge::onEvent() {
    // Blocking client fd (intentionally not SOCK_NONBLOCK): loopback
    // payloads are a couple of KB, so short reads complete instantly.
    int client = accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) return;

    // Read headers + (for POST) body, bounded.
    char buf[16384];
    size_t have = 0;
    size_t want_body = 0;
    size_t header_end = 0;
    for (int iter = 0; iter < 16; ++iter) {
        ssize_t n = read(client, buf + have, sizeof(buf) - 1 - have);
        if (n <= 0) break;
        have += static_cast<size_t>(n);
        buf[have] = '\0';
        if (!header_end) {
            const char* e = strstr(buf, "\r\n\r\n");
            if (!e) continue;
            header_end = static_cast<size_t>(e - buf) + 4;
            const char* cl = strcasestr(buf, "Content-Length:");
            if (cl && cl < e) want_body = strtoul(cl + 15, nullptr, 10);
        }
        if (have >= header_end + want_body) break;
    }
    if (have == 0) { close(client); return; }

    // CORS + Private Network Access: browser-sandboxed clients fetch
    // from an https origin, and Chrome 142+ blocks public ->
    // private-network requests unless the preflight explicitly
    // allows it. Answer OPTIONS without touching the handler.
    const char* cors =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Private-Network: true\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: *\r\n";
    if (strncmp(buf, "OPTIONS ", 8) == 0) {
        char head[512];
        const int hl = snprintf(head, sizeof(head),
                                "HTTP/1.1 204 No Content\r\n%s"
                                "Content-Length: 0\r\nConnection: close\r\n\r\n",
                                cors);
        ssize_t unused = write(client, head, static_cast<size_t>(hl));
        (void)unused;
        close(client);
        return;
    }

    // "GET|POST /hue/190/12" -> command "hue 190 12"; a POST body is
    // appended as the command's last word (used by /art/<w>/<h>).
    std::string body = "err bad request";
    const bool is_get = strncmp(buf, "GET /", 5) == 0;
    const bool is_post = strncmp(buf, "POST /", 6) == 0;
    if (is_get || is_post) {
        const char* p = buf + (is_get ? 5 : 6);
        std::string cmd;
        while (*p && *p != ' ' && *p != '?')
            cmd += (*p == '/') ? ' ' : *p, ++p;
        if (is_post && header_end && have > header_end) {
            cmd += ' ';
            cmd.append(buf + header_end, have - header_end);
        }
        if (!cmd.empty()) {
            log_info("HTTP bridge: %s",
                     cmd.size() > 64 ? (cmd.substr(0, 61) + "...").c_str()
                                     : cmd.c_str());
            body = handler_ ? handler_(cmd) : "err no handler";
        }
    }

    char head[512];
    const int hl = snprintf(head, sizeof(head),
                            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n%s"
                            "Content-Length: %zu\r\nConnection: close\r\n\r\n",
                            cors, body.size());
    ssize_t unused = write(client, head, static_cast<size_t>(hl));
    unused = write(client, body.data(), body.size());
    (void)unused;
    close(client);
}
