#include "engine/event_loop.hpp"
#include "engine/scheduler.hpp"
#include "drm/drm_device.hpp"
#include "drm/drm_output.hpp"
#include "output/output.hpp"
#include "output/dump_output.hpp"
#ifdef TOUCHBAR_FX_SCREEN
#include "output/sdl_output.hpp"
#endif
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include "config/config.hpp"
#include "audio/audio_source.hpp"
#include "audio/pulse_monitor_source.hpp"
#include "audio/null_audio_source.hpp"
#include "media/fake_mpris_client.hpp"
#include "widget/widget.hpp"
#include "widget/layout_engine.hpp"
#include "mode/mode.hpp"
#include "mode/mode_registry.hpp"
#include "mode/mode_manager.hpp"
#include "ipc/ipc_server.hpp"
#include "ipc/http_bridge.hpp"
#include "util/log.hpp"
#include "input/touch_input.hpp"
#include "util/timer_fd.hpp"

#include <sys/signalfd.h>
#include <sys/epoll.h>
#include <signal.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

static bool stop_tiny_dfr() {
    if (system("pgrep -x tiny-dfr > /dev/null 2>&1") == 0) {
        log_info("Stopping tiny-dfr...");
        system("systemctl stop tiny-dfr 2>/dev/null");
        usleep(300000);
        if (system("pgrep -x tiny-dfr > /dev/null 2>&1") == 0) {
            system("pkill -TERM tiny-dfr 2>/dev/null");
            usleep(500000);
        }
        return true;
    }
    return false;
}

static void start_tiny_dfr() {
    log_info("Restarting tiny-dfr...");
    system("systemctl start tiny-dfr 2>/dev/null");
}

static void print_usage(const char* argv0) {
    printf(
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -d, --debug          verbose logging\n"
        "  -h, --help           this help\n"
        "      --config PATH    config file (default ~/.config/touchbar-fx/config.toml)\n"
        "      --null-audio     synthetic test signal instead of PipeWire monitor\n"
        "      --screen         on-screen SDL2 window (needs -DTOUCHBAR_FX_SCREEN=ON)\n"
        "\n"
        "Headless frame dump (no touch bar hardware needed):\n"
        "      --dump-frames N  render N frames of the media center and exit\n"
        "      --out DIR        dump directory (default /tmp/tbfx-frames)\n"
        "      --width W        dump width  (default [dump] width, 2170)\n"
        "      --height H       dump height (default [dump] height, 60)\n"
        "\n"
        "Example: %s --dump-frames 300 --null-audio --out /tmp/tbfx-frames\n",
        argv0, argv0);
}

// Headless harness: render the media center mode (transport |
// visualizer + fake track overlay | volume slider) into BMP files.
static int runDump(const Config& cfg, int frames, const std::string& out_dir,
                   bool null_audio, int w, int h) {
    log_info("Dump mode: %d frames at %dx%d -> %s (%s)",
             frames, w, h, out_dir.c_str(),
             null_audio ? "null audio" : "live monitor");

    Framebuffer composite(w, h);
    DumpOutput output(out_dir);
    EventLoop loop;
    LayoutEngine layout(w, h);
    Scheduler scheduler(loop, output, layout, composite, 16);

    std::unique_ptr<AudioSource> source = null_audio
        ? std::unique_ptr<AudioSource>(std::make_unique<NullAudioSource>())
        : std::unique_ptr<AudioSource>(std::make_unique<PulseMonitorSource>());

    ModeContext ctx{loop, cfg, source, nullptr};
    ctx.mpris_factory = [] { return std::make_unique<FakeMprisClient>(); };

    std::unique_ptr<Mode> mode = createMode("mediacenter", ctx);
    if (!mode || mode->widgets().empty()) {
        log_error("Dump mode: mediacenter mode unavailable");
        return 1;
    }
    layout.setWidgetsBorrowed(mode->widgets(), scheduler);

    auto widgets = mode->widgets();
    for (int i = 0; i < frames; ++i) {
        for (auto* wp : widgets) wp->update();
        layout.render(composite);
        output.present(composite);
    }

    log_info("Wrote %d frames", output.framesWritten());
    return 0;
}

int main(int argc, char* argv[]) {
    bool debug = false;
    bool null_audio = false;
    bool screen = false;
    int dump_frames = 0;
    std::string out_dir = "/tmp/tbfx-frames";
    std::string config_path;
    int width = 0, height = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                log_error("%s requires a value", name);
                exit(2);
            }
            return argv[++i];
        };
        if (arg == "--debug" || arg == "-d") debug = true;
        else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
        else if (arg == "--null-audio") null_audio = true;
        else if (arg == "--screen") screen = true;
        else if (arg == "--dump-frames") dump_frames = atoi(next("--dump-frames").c_str());
        else if (arg == "--out") out_dir = next("--out");
        else if (arg == "--config") config_path = next("--config");
        else if (arg == "--width") width = atoi(next("--width").c_str());
        else if (arg == "--height") height = atoi(next("--height").c_str());
        else {
            log_error("Unknown option: %s", arg.c_str());
            print_usage(argv[0]);
            return 2;
        }
    }

    if (debug) log_set_level(LogLevel::Debug);

    Config cfg = Config::load(config_path.empty() ? Config::defaultPath()
                                                  : config_path);
    theme::apply(Theme::fromName(cfg.ui_theme), cfg.ui_theme);
    if (width <= 0) width = cfg.dump_width;
    if (height <= 0) height = cfg.dump_height;

    if (dump_frames > 0)
        return runDump(cfg, dump_frames, out_dir, null_audio, width, height);

    log_info("touchbar-fx v0.3.0 starting");

    // Block signals we want to handle via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    int sig_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sig_fd < 0) {
        log_error("signalfd failed");
        return 1;
    }

    // Output backend: touch bar DRM (default) or SDL2 window
    Output* output = nullptr;
    int w = width, h = height;
    bool had_tiny_dfr = false;

    DrmDevice drm_dev;
    DrmOutput drm_out(drm_dev);
#ifdef TOUCHBAR_FX_SCREEN
    SdlOutput sdl_out;
#endif

    if (screen) {
#ifdef TOUCHBAR_FX_SCREEN
        if (!sdl_out.init(w, h)) {
            close(sig_fd);
            return 1;
        }
        output = &sdl_out;
        log_info("SDL2 window backend: %dx%d", w, h);
#else
        log_error("--screen needs SDL2: rebuild with -DTOUCHBAR_FX_SCREEN=ON");
        close(sig_fd);
        return 1;
#endif
    } else {
        // Stop tiny-dfr so we can take DRM master
        had_tiny_dfr = stop_tiny_dfr();

        if (!drm_dev.open()) {
            log_error("Failed to open Touch Bar DRM device");
            if (had_tiny_dfr) start_tiny_dfr();
            close(sig_fd);
            return 1;
        }
        if (!drm_out.init()) {
            log_error("Failed to initialize DRM output");
            drm_dev.close();
            if (had_tiny_dfr) start_tiny_dfr();
            close(sig_fd);
            return 1;
        }
        output = &drm_out;
        w = drm_dev.width();
        h = drm_dev.height();
    }

    // Create the composite framebuffer from the actual output geometry
    Framebuffer composite(w, h);

    // Event loop
    EventLoop loop;

    // Layout engine
    LayoutEngine layout(w, h);

    // Scheduler (dirty-coalescing, 16ms = ~60fps max)
    Scheduler scheduler(loop, *output, layout, composite, 16);

    // Audio for the visualizer: PipeWire monitor of the default sink,
    // or a synthetic signal with --null-audio. The media center mode
    // takes ownership via the mode context.
    std::unique_ptr<AudioSource> source = null_audio
        ? std::unique_ptr<AudioSource>(std::make_unique<NullAudioSource>())
        : std::unique_ptr<AudioSource>(std::make_unique<PulseMonitorSource>());

    // Exclusive full-bar modes from the registry, in TOML order
    ModeContext ctx{loop, cfg, source, nullptr};
    ModeManager modes(loop, layout, scheduler, cfg, ctx);
    modes.start();

    // IPC socket for touchbar-fxctl / keybinds
    IpcServer ipc(loop);
    if (ipc.init()) {
        ipc.setHandler([&modes](const std::string& cmd) {
            return modes.handleCommand(cmd);
        });
    }

    // HTTP bridge (127.0.0.1) for clients that can't reach the unix
    // socket — an external/manual override path for hue and art.
    HttpBridge http(loop);
    if (http.init()) {
        http.setHandler([&modes](const std::string& cmd) {
            return modes.handleCommand(cmd);
        });
    }

    // Touch input: taps, long-presses and drags go to widgets (modes
    // switch via the mode button, presets via the ✦ button — this
    // digitizer never emits Y events, so no swipe gestures exist)
    TouchInput touch(loop, layout);
    if (!touch.init()) {
        log_warn("Touch input unavailable — tap controls disabled");
    }

#ifdef TOUCHBAR_FX_SCREEN
    // Poll for SDL window close
    int sdl_poll_fd = -1;
    if (screen) {
        sdl_poll_fd = timerfd_create_ms(100, true);
        if (sdl_poll_fd >= 0) {
            loop.addFd(sdl_poll_fd, EPOLLIN, [&](uint32_t) {
                timerfd_read(sdl_poll_fd);
                if (sdl_out.quitRequested()) loop.stop();
            });
        }
    }
#endif

    // Signal handler for clean shutdown
    loop.addFd(sig_fd, EPOLLIN, [&loop, sig_fd](uint32_t) {
        struct signalfd_siginfo info;
        if (read(sig_fd, &info, sizeof(info)) == sizeof(info)) {
            log_info("Received signal %d, shutting down", info.ssi_signo);
            loop.stop();
        }
    });

    // Do an initial render
    scheduler.markDirty();
    scheduler.flush();

    log_info("Running (Ctrl+C to stop)");
    loop.run();

    // Cleanup
    log_info("Shutting down");
    composite.clear(0xFF000000);
    output->present(composite);
    if (!screen) drm_dev.close();

    if (had_tiny_dfr) start_tiny_dfr();

    close(sig_fd);
    log_info("Goodbye");
    return 0;
}
