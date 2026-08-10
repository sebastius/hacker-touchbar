// touchbar-fxctl — tiny CLI that sends one-word commands to a running
// touchbar-fx daemon over its Unix socket, e.g. from a Hyprland
// keybind:
//   bind = , XF86AudioMedia, exec, touchbar-fxctl cycle
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static std::string socket_path() {
    const char* env = getenv("TOUCHBAR_FX_SOCK");
    if (env && *env) return env;
    const char* xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg) return std::string(xdg) + "/touchbar-fx.sock";
    return "/tmp/touchbar-fx-" + std::to_string(getuid()) + ".sock";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: touchbar-fxctl <command> [args...]\n"
            "Commands:\n"
            "  cycle                       next mode\n"
            "  mode                        print current mode\n"
            "  mode mediacenter|sliders    switch mode\n"
            "  preset                      print current visualizer preset\n"
            "  preset next|prev            cycle visualizer preset\n"
            "  preset <name>               switch visualizer preset\n"
            "Socket: $TOUCHBAR_FX_SOCK or $XDG_RUNTIME_DIR/touchbar-fx.sock\n");
        return 2;
    }

    std::string cmd = argv[1];
    for (int i = 2; i < argc; ++i) {
        cmd += ' ';
        cmd += argv[i];
    }

    std::string path = socket_path();
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        fprintf(stderr, "touchbar-fxctl: cannot connect to %s: %s\n"
                "Is touchbar-fx running?\n", path.c_str(), strerror(errno));
        close(fd);
        return 1;
    }

    cmd += '\n';
    if (write(fd, cmd.data(), cmd.size()) < 0) {
        perror("write");
        close(fd);
        return 1;
    }
    shutdown(fd, SHUT_WR);

    char buf[512];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        fprintf(stderr, "touchbar-fxctl: no reply\n");
        return 1;
    }
    buf[n] = '\0';
    fputs(buf, stdout);
    return strncmp(buf, "ok", 2) == 0 ? 0 : 1;
}
