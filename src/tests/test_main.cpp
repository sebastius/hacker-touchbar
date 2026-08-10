// Headless unit tests for touchbar-fx. No touch bar, PulseAudio or
// D-Bus needed: drag math is exercised through the widgets with a
// FakeMprisClient, and preset distinctness is checked by rendering
// real frames from the synthetic audio source.
#include "input/gesture.hpp"
#include "input/touch_input.hpp"
#include "config/config.hpp"
#include "audio/null_audio_source.hpp"
#include "audio/pulse_monitor_source.hpp"
#include "engine/event_loop.hpp"
#include "engine/scheduler.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include "media/fake_mpris_client.hpp"
#include "mode/mode.hpp"
#include "mode/mode_registry.hpp"
#include "mode/mode_manager.hpp"
#include "output/dump_output.hpp"
#include "viz/preset.hpp"
#include "viz/preset_registry.hpp"
#include "viz/viz_renderer.hpp"
#include "widget/layout_engine.hpp"
#include "widget/slider_widget.hpp"
#include "widget/builtin/brightness_widget.hpp"
#include "widget/builtin/transport_widget.hpp"
#include "widget/builtin/viz_widget.hpp"
#include "viz/pioneer_hues.hpp"
#include "viz/album_art.hpp"
#include "media/art_analyze.hpp"
#include "gfx/theme.hpp"
#include "widget/builtin/volume_slider_widget.hpp"
#include "widget/widget.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <linux/input.h>
#include <unistd.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do {                                            \
        ++checks;                                                   \
        if (!(cond)) {                                              \
            ++failures;                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                           \
    } while (0)

#define CHECK_EQ(a, b) do {                                         \
        ++checks;                                                   \
        auto va = (a); auto vb = (b);                               \
        if (!(va == vb)) {                                          \
            ++failures;                                             \
            printf("FAIL %s:%d: %s == %s failed\n",                \
                   __FILE__, __LINE__, #a, #b);                     \
        }                                                           \
    } while (0)

// ---- gesture classification ----

static void test_gestures() {
    CHECK(classifyGesture(130, 10, false).gesture == Gesture::SwipeHorizontal);
    CHECK(classifyGesture(130, 10, false).dir == 1);
    CHECK(classifyGesture(-130, 5, false).gesture == Gesture::SwipeHorizontal);
    CHECK(classifyGesture(-130, 5, false).dir == -1);
    CHECK(classifyGesture(3, 2, false).gesture == Gesture::Undecided);
    CHECK(classifyGesture(15, 2, true).gesture == Gesture::Drag);
    CHECK(classifyGesture(15, 2, false).gesture == Gesture::Undecided);
    // no vertical gestures exist on this digitizer (Y is never emitted)
    CHECK(classifyGesture(5, 35, false).gesture == Gesture::Undecided);
    CHECK(classifyGesture(5, 35, true).gesture == Gesture::Undecided);
    // drag requires horizontal dominance
    CHECK(classifyGesture(15, 20, true).gesture == Gesture::Undecided);
}

// ---- volume slider drag math ----

static void test_volume_drag() {
    VolumeSliderWidget vol(100, 300);   // no init(): offline, local state
    vol.setAllocatedWidth(300);
    const int x0 = VolumeSliderWidget::TRACK_X0;             // 56
    const int x1 = 300 - VolumeSliderWidget::TRACK_PAD;      // 292
    CHECK_EQ(vol.percentForX(x0), 0);
    CHECK_EQ(vol.percentForX(x1), 100);
    CHECK_EQ(vol.percentForX((x0 + x1) / 2), 50);
    CHECK_EQ(vol.percentForX(0), 0);         // clamped left
    CHECK_EQ(vol.percentForX(10000), 100);   // clamped right

    // Dragging on the track maps finger x directly to the volume.
    vol.onDrag(x0, 30, DragPhase::Begin);
    vol.onDrag((x0 + x1) / 2, 30, DragPhase::Move);
    CHECK_EQ(vol.volumePercent(), 50);
    vol.onDrag(x1, 30, DragPhase::Move);
    vol.onDrag(x1, 30, DragPhase::End);      // End must not re-apply
    CHECK_EQ(vol.volumePercent(), 100);

    // The whole zone is drag-capable, at any height.
    CHECK(vol.dragCapableAt(10, 5));
    CHECK(vol.dragCapableAt(150, 55));

    // A drag starting on the dead zone (mute pill) follows relatively
    // instead of jumping to 0.
    vol.onTap((x0 + x1) / 2, 30);            // absolute tap: 50%
    CHECK_EQ(vol.volumePercent(), 50);
    vol.onDrag(10, 30, DragPhase::Begin);    // grab the mute-pill area
    CHECK_EQ(vol.volumePercent(), 50);       // no jump
    vol.onDrag(128, 30, DragPhase::Move);    // percentForX(128)=30 -> 50+30
    CHECK_EQ(vol.volumePercent(), 80);
    vol.onDrag(128, 30, DragPhase::End);

    // [volume] max_percent scales the range.
    VolumeSliderWidget vol150(150, 300);
    vol150.setAllocatedWidth(300);
    CHECK_EQ(vol150.percentForX(x1), 150);
    CHECK_EQ(vol150.percentForX((x0 + x1) / 2), 75);
}

// ---- tap routing: the REAL digitizer stream ----
//
// Replays the event shapes from the raw evdev capture of the actual
// Touch Bar digitizer: down frames are TRACKING_ID + BTN_TOUCH (+
// optionally MT_X/ABS_X), Y is never emitted, slot numbers wander.
// The press position comes from the cached last-seen X — evdev only
// omits an axis event when its value is unchanged, so the cache is
// always correct (this driver's EVIOCGABS reads 0; ioctls are gone).
static void test_tap_routing() {
    EventLoop loop;
    Framebuffer composite(500, 60);
    DumpOutput output("/tmp/tbfx-test-frames");
    LayoutEngine layout(500, 60);
    Scheduler sched(loop, output, layout, composite, 16);

    TransportWidget transport(200);
    FakeMprisClient mpris;
    transport.setClient(&mpris);
    transport.sync(mpris.state());  // has_player, playing
    VolumeSliderWidget volume(100, 300);

    std::vector<Widget*> widgets = {&transport, &volume};
    layout.setWidgetsBorrowed(widgets, sched);  // [0..200) | [200..500)

    TouchInput touch(loop, layout);  // no init(): synthetic events only
    touch.setAxisRangesForTest(0, 500, 0, 60);  // raw == logical pixels

    auto abs = [&](int code, int val) { touch.onAbsEvent(code, val); };
    auto key = [&](int code, int val) { touch.onKeyEvent(code, val); };
    auto syn = [&] { touch.onSynReport(); };

    // (a) Tap with MT_X + ABS_X in the down frame (typical capture
    // frame): hit at that x.
    abs(ABS_MT_SLOT, 5);
    abs(ABS_MT_TRACKING_ID, 405);
    abs(ABS_MT_POSITION_X, 100);
    key(BTN_TOUCH, 1);
    key(BTN_TOOL_FINGER, 1);
    abs(ABS_X, 100);
    syn();
    abs(ABS_MT_TRACKING_ID, -1);
    key(BTN_TOUCH, 0);
    key(BTN_TOOL_FINGER, 0);
    syn();
    CHECK(!mpris.state().playing);          // play/pause fired
    CHECK_EQ(volume.volumePercent(), 0);    // volume never touched

    // (b) Tap with NO position events at all, after a previous touch
    // at x=450 (volume slider): the cached X must hit the volume
    // slider again at the same spot — at 450, not 0.
    mpris.playPause();  // back to playing
    abs(ABS_MT_TRACKING_ID, 410);
    abs(ABS_MT_POSITION_X, 450);
    key(BTN_TOUCH, 1);
    syn();
    abs(ABS_MT_TRACKING_ID, -1);
    key(BTN_TOUCH, 0);
    syn();
    int vol_after_first_tap = volume.volumePercent();
    CHECK(vol_after_first_tap > 0);         // tapped mid-track
    CHECK(mpris.state().playing);           // transport NOT fired

    abs(ABS_MT_TRACKING_ID, 417);           // same-X tap: no position
    key(BTN_TOUCH, 1);                      // events in this frame
    syn();
    abs(ABS_MT_TRACKING_ID, -1);
    key(BTN_TOUCH, 0);
    syn();
    CHECK_EQ(volume.volumePercent(), vol_after_first_tap);
    CHECK(mpris.state().playing);

    // (c) The original volume-0 case: a drag ends on the volume
    // slider, then a tap on the transport. Per the evdev invariant
    // the tap's down frame carries MT_X (the value changed), exactly
    // as in the capture.
    abs(ABS_MT_SLOT, 8);
    abs(ABS_MT_TRACKING_ID, 421);
    abs(ABS_MT_POSITION_X, 400);
    key(BTN_TOUCH, 1);
    syn();
    for (int x = 385; x >= 300; x -= 15) {  // drag left across frames
        abs(ABS_MT_POSITION_X, x);
        syn();
    }
    abs(ABS_MT_TRACKING_ID, -1);
    key(BTN_TOUCH, 0);
    syn();
    int vol_after_drag = volume.volumePercent();
    CHECK(vol_after_drag > 0);
    CHECK(vol_after_drag < vol_after_first_tap);

    abs(ABS_MT_TRACKING_ID, 450);           // transport tap
    abs(ABS_MT_POSITION_X, 100);
    key(BTN_TOUCH, 1);
    syn();
    abs(ABS_MT_TRACKING_ID, -1);
    key(BTN_TOUCH, 0);
    syn();
    CHECK(!mpris.state().playing);               // transport fired
    CHECK_EQ(volume.volumePercent(), vol_after_drag);  // volume intact
}

// ---- first-ever touch without any X event lands mid-bar ----

namespace {
class TapRecorder : public Widget {
public:
    TapRecorder() { name_ = "recorder"; }
    void update() override {}
    void render(Framebuffer&) override {}
    void onTap(int x, int) override { last_tap_x = x; }
    int last_tap_x = -1;
};
}  // namespace

static void test_first_touch_center() {
    EventLoop loop;
    Framebuffer composite(500, 60);
    DumpOutput output("/tmp/tbfx-test-frames");
    LayoutEngine layout(500, 60);
    Scheduler sched(loop, output, layout, composite, 16);

    TapRecorder rec;
    std::vector<Widget*> widgets = {&rec};
    layout.setWidgetsBorrowed(widgets, sched);

    TouchInput touch(loop, layout);
    touch.setAxisRangesForTest(0, 500, 0, 60);

    // No X event has EVER been seen; the down frame carries none
    // either. The tap must land at the device center (250), not 0.
    touch.onAbsEvent(ABS_MT_TRACKING_ID, 1);
    touch.onKeyEvent(BTN_TOUCH, 1);
    touch.onSynReport();
    touch.onAbsEvent(ABS_MT_TRACKING_ID, -1);
    touch.onKeyEvent(BTN_TOUCH, 0);
    touch.onSynReport();
    CHECK_EQ(rec.last_tap_x, 250);
}

// ---- seek strip drag math ----

static void test_seek_drag() {
    Config cfg;
    Framebuffer fb(1000, 60);
    VizWidget viz(std::make_unique<NullAudioSource>(), cfg);
    viz.setAllocatedWidth(1000);
    FakeMprisClient mpris;
    viz.setMpris(&mpris);

    viz.render(fb);  // creates the renderer

    // Hit-testing is X-only (the digitizer never reports Y).
    CHECK(viz.dragCapableAt(500, 59));
    CHECK(viz.dragCapableAt(500, 48));
    CHECK(viz.dragCapableAt(500, 30));

    // x maps linearly to track position (fake length: 227 s).
    viz.onDrag(500, 59, DragPhase::Begin);
    CHECK_EQ(mpris.seek_count, 1);
    int64_t expect = 227'000'000LL * 500 / 1000;
    int64_t got = mpris.last_seek_us;
    CHECK(got > expect - 2'000'000 && got < expect + 2'000'000);

    // Clamped at both edges (Move events are throttled to 80 ms).
    usleep(90'000);
    viz.onDrag(-50, 59, DragPhase::Move);
    CHECK_EQ(mpris.last_seek_us, 0);
    usleep(90'000);
    viz.onDrag(9999, 59, DragPhase::Move);
    CHECK_EQ(mpris.last_seek_us, 227'000'000LL * 999 / 1000);
}

// ---- preset registry + distinct frames ----

static void renderFrames(VizRenderer& viz, NullAudioSource& src,
                         Framebuffer& fb, int frames) {
    float buf[800];
    for (int i = 0; i < frames; ++i) {
        size_t n = src.read(buf, 800);
        viz.feedSamples(buf, n, src.sampleRate());
        viz.render(fb);
    }
}

static void test_presets() {
    auto names = presetNames();
    bool has_waves = false, has_scope = false, has_dolphins = false,
         has_spectrum = false, has_pioneer = false, has_penguin = false;
    for (auto& n : names) {
        if (n == "waves") has_waves = true;
        if (n == "oscilloscope") has_scope = true;
        if (n == "dolphins") has_dolphins = true;
        if (n == "spectrum") has_spectrum = true;
        if (n == "pioneer") has_pioneer = true;
        if (n == "penguin") has_penguin = true;
    }
    CHECK(has_waves);
    CHECK(has_scope);
    CHECK(has_dolphins);
    CHECK(has_spectrum);
    CHECK(has_pioneer);
    CHECK(has_penguin);
    CHECK(createPreset("does-not-exist") == nullptr);

    Config cfg;
    Framebuffer fb_a(640, 60), fb_b(640, 60);
    NullAudioSource src_a, src_b;  // identical synthetic sequences
    VizRenderer viz_a(640, 60, cfg, "waves");
    VizRenderer viz_b(640, 60, cfg, "oscilloscope");
    renderFrames(viz_a, src_a, fb_a, 90);
    renderFrames(viz_b, src_b, fb_b, 90);

    // The two presets must produce distinctly different frames.
    int differ = 0;
    int total = 640 * 60;
    for (int i = 0; i < total; ++i)
        if (fb_a.data()[i] != fb_b.data()[i]) ++differ;
    printf("preset frames differ in %d/%d pixels\n", differ, total);
    CHECK(differ > total / 10);

    // dolphins smoke check: renders real content (sprites + water
    // line + splashes), far from a blank frame...
    Framebuffer fb_c(640, 60);
    NullAudioSource src_c;
    VizRenderer viz_c(640, 60, cfg, "dolphins");
    renderFrames(viz_c, src_c, fb_c, 90);
    int lit = 0;
    for (int i = 0; i < total; ++i)
        if (fb_c.data()[i] != 0xFF000000) ++lit;
    printf("dolphins frame: %d/%d lit pixels\n", lit, total);
    CHECK(lit > 400);

    // ...and the scene moves: 30 frames later pixels must change.
    std::vector<uint32_t> before(fb_c.data(), fb_c.data() + total);
    renderFrames(viz_c, src_c, fb_c, 30);
    int moved = 0;
    for (int i = 0; i < total; ++i)
        if (fb_c.data()[i] != before[i]) ++moved;
    printf("dolphins frames moved %d/%d pixels in 30 frames\n", moved, total);
    CHECK(moved > 500);

    // spectrum smoke check: segmented analyzer renders real content,
    // and after loud frames something is lit in the top quarter (tall
    // columns + peak-hold dots).
    Framebuffer fb_s(640, 60);
    NullAudioSource src_s;
    VizRenderer viz_s(640, 60, cfg, "spectrum");
    renderFrames(viz_s, src_s, fb_s, 90);
    int slit = 0, top_lit = 0;
    for (int i = 0; i < total; ++i) {
        if (fb_s.data()[i] != 0xFF000000) {
            ++slit;
            if (i / 640 < 15) ++top_lit;  // y < 15: top quarter
        }
    }
    printf("spectrum frame: %d/%d lit pixels, %d in top quarter\n",
           slit, total, top_lit);
    CHECK(slit > 400);
    CHECK(top_lit > 0);

    // pioneer smoke check: decodes the real .lkd clips from the repo
    // (located relative to this source file) and renders moving
    // content (time-based 14 fps source playback).
    {
        std::string clip_dir = __FILE__;
        clip_dir = clip_dir.substr(0, clip_dir.find_last_of('/'));
        clip_dir += "/../../assets/clips";
        setenv("TOUCHBAR_FX_CLIP_DIR", clip_dir.c_str(), 1);
        Framebuffer fb_p(640, 60);
        NullAudioSource src_p;
        VizRenderer viz_p(640, 60, cfg, "pioneer");
        renderFrames(viz_p, src_p, fb_p, 40);
        int plit = 0;
        for (int i = 0; i < total; ++i)
            if (fb_p.data()[i] != 0xFF000000) ++plit;
        printf("pioneer frame: %d/%d lit pixels\n", plit, total);
        // 8 aspect-correct panels cover ~1/3 of the frame; the dense
        // cyan imagery lights most of that.
        CHECK(plit > total / 6);

        std::vector<uint32_t> pbefore(fb_p.data(), fb_p.data() + total);
        // Playback is time-based, so let the clock advance a few
        // source frames instead of just rendering more frames.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        renderFrames(viz_p, src_p, fb_p, 8);
        int pmoved = 0;
        for (int i = 0; i < total; ++i)
            if (fb_p.data()[i] != pbefore[i]) ++pmoved;
        printf("pioneer frames moved %d/%d pixels in 8 frames\n",
               pmoved, total);
        CHECK(pmoved > 500);

        // Tap a panel (panel 3 of 8 at 640 px wide) -> focus mode:
        // full-bright stock-cyan backdrop behind an accent-color
        // (amber) center window.
        viz_p.onTap(3 * 640 / 8 + 10, 30);
        renderFrames(viz_p, src_p, fb_p, 8);
        int amber = 0, cyan = 0;
        for (int i = 0; i < total; ++i) {
            const uint32_t px = fb_p.data()[i];
            const int r = (px >> 16) & 0xFF, b = px & 0xFF;
            if (r > 60 && b < r / 2) ++amber;
            if (b > 60 && r < b / 2) ++cyan;
        }
        printf("pioneer focus: %d amber + %d cyan pixels\n", amber, cyan);
        CHECK(amber > 1000);  // accent center window
        CHECK(cyan > 1000);   // dim backdrop covers most of the frame

        // Tap again -> back to the wall (all-cyan panels, no amber).
        viz_p.onTap(320, 30);
        renderFrames(viz_p, src_p, fb_p, 8);
        amber = 0;
        for (int i = 0; i < total; ++i) {
            const uint32_t px = fb_p.data()[i];
            const int r = (px >> 16) & 0xFF, b = px & 0xFF;
            if (r > 60 && b < r / 2) ++amber;
        }
        printf("pioneer back to wall: %d amber pixels\n", amber);
        CHECK(amber == 0);
    }

    // penguin smoke check: the flock renders real sprites on the
    // ground line, and the scene animates.
    {
        Framebuffer fb_g(640, 60);
        NullAudioSource src_g;
        VizRenderer viz_g(640, 60, cfg, "penguin");
        renderFrames(viz_g, src_g, fb_g, 40);
        int glit = 0;
        for (int i = 0; i < total; ++i)
            if (fb_g.data()[i] != 0xFF000000) ++glit;
        printf("penguin frame: %d/%d lit pixels\n", glit, total);
        CHECK(glit > 800);  // ten sprites + ground line

        std::vector<uint32_t> gbefore(fb_g.data(), fb_g.data() + total);
        renderFrames(viz_g, src_g, fb_g, 12);
        int gmoved = 0;
        for (int i = 0; i < total; ++i)
            if (fb_g.data()[i] != gbefore[i]) ++gmoved;
        printf("penguin frames moved %d/%d pixels in 12 frames\n",
               gmoved, total);
        CHECK(gmoved > 200);
    }

    // penguin 808 reaction: a bass onset launches the flock — most
    // of the ten penguins should be airborne right after the hit.
    {
        Framebuffer fb8(640, 60);
        VizRenderer viz8(640, 60, cfg, "penguin");
        std::vector<float> silence(1024, 0.0f);
        for (int i = 0; i < 40; ++i) {
            viz8.feedSamples(silence.data(), silence.size(), 48000);
            viz8.render(fb8);
        }
        CHECK(viz8.airborneCount() == 0);  // settled in silence
        // 808: sustained 45 Hz sine — the onset fires on the rise.
        int max_airborne = 0;
        double phase = 0.0;
        std::vector<float> burst(1024);
        for (int f = 0; f < 25; ++f) {
            for (size_t i = 0; i < burst.size(); ++i) {
                burst[i] = static_cast<float>(0.9 * std::sin(phase));
                phase += 2.0 * M_PI * 45.0 / 48000.0;
            }
            viz8.feedSamples(burst.data(), burst.size(), 48000);
            viz8.render(fb8);
            max_airborne = std::max(max_airborne, viz8.airborneCount());
        }
        printf("penguin 808: up to %d penguins airborne on the hit\n",
               max_airborne);
        CHECK(max_airborne >= 5);
    }

    // penguin idle socials: left in silence, the flock wanders to
    // each other and interacts — hearts rise into rows a grounded
    // penguin can never reach (hop apex tops out at row ~8).
    {
        Framebuffer fb_s2(640, 60);
        VizRenderer viz_s2(640, 60, cfg, "penguin");
        std::vector<float> silence(1024, 0.0f);
        int social_top = 0;
        long social_frame = -1;
        for (int f = 0; f < 3000 && social_top == 0; ++f) {
            viz_s2.feedSamples(silence.data(), silence.size(), 48000);
            viz_s2.render(fb_s2);
            int n = 0;
            for (int i = 0; i < 640 * 8; ++i)
                if (fb_s2.data()[i] != 0xFF000000) ++n;
            if (n > 0) { social_top = n; social_frame = f; }
        }
        printf("penguin social: %d lit pixels in rows 0-7 (frame %ld)\n",
               social_top, social_frame);
        CHECK(social_top > 0);
    }

    // Unknown preset name keeps the current one. Rotation order is
    // alphabetical: dolphins -> oscilloscope -> penguin -> pioneer ->
    // spectrum -> waves.
    CHECK(!viz_a.setPreset("does-not-exist"));
    CHECK(viz_a.presetName() == "waves");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "dolphins");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "oscilloscope");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "penguin");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "pioneer");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "spectrum");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "waves");
}

// ---- config ----

static void test_config() {
    Config def = Config::load("/nonexistent/touchbar-fx-test.toml");
    CHECK_EQ(def.mc_transport_width, 200);
    CHECK_EQ(def.mc_volume_width, 300);
    CHECK_EQ(def.mc_flash_seconds, 2);
    CHECK_EQ(def.viz_rotate_seconds, 30);
    CHECK_EQ(def.volume_max_percent, 100);
    CHECK(def.ui_theme == "default");
    CHECK(def.viz_preset == "waves");
    CHECK(def.mode_order.size() == 2 && def.mode_order[0] == "mediacenter");

    const char* path = "/tmp/tbfx-test-config.toml";
    FILE* f = fopen(path, "w");
    fputs("[modes]\norder = [\"mediacenter\", \"sliders\", \"bogus\"]\n"
          "[ui]\ntheme = \"oel\"\n"
          "[mediacenter]\ntransport_width = 240\nvolume_width = 360\n"
          "flash_seconds = 5\n"
          "[visualizer]\npreset = \"oscilloscope\"\nrotate_seconds = 45\n"
          "[volume]\nmax_percent = 150\n", f);
    fclose(f);
    Config cfg = Config::load(path);
    CHECK_EQ(cfg.mc_transport_width, 240);
    CHECK_EQ(cfg.mc_volume_width, 360);
    CHECK_EQ(cfg.mc_flash_seconds, 5);
    CHECK_EQ(cfg.viz_rotate_seconds, 45);
    CHECK_EQ(cfg.volume_max_percent, 150);
    CHECK(cfg.ui_theme == "oel");
    CHECK(cfg.viz_preset == "oscilloscope");
    CHECK(cfg.mode_order.size() == 3);
}

// ---- theme palettes ----

static void test_theme() {
    Theme def = Theme::fromName("default");
    Theme oel = Theme::fromName("oel");
    CHECK(def.green != oel.green);        // distinct palettes
    CHECK(!def.zone_frame);
    CHECK(oel.zone_frame);                // OEL draws zone frames

    // Unknown names fall back to the default palette (with a warning).
    Theme fb = Theme::fromName("bogus-theme");
    CHECK(fb.green == def.green);
    CHECK(fb.accent == def.accent);
    CHECK(fb.zone_frame == def.zone_frame);

    // apply() swaps the live palette; restore default afterwards.
    theme::apply(oel);
    CHECK(theme::green == oel.green);
    theme::apply(def);
    CHECK(theme::green == def.green);
}

// ---- brightness slider ----

static void test_brightness_slider() {
    if (!BrightnessWidget::available()) {
        printf("no backlight device — brightness slider test skipped\n");
        return;
    }
    BrightnessWidget b;  // sysfs writes fail silently as non-root
    CHECK(b.init());
    b.setAllocatedWidth(940);

    // Whole zone drag-capable at any height; x maps to 0..100%.
    CHECK(b.dragCapableAt(0, 5));
    CHECK(b.dragCapableAt(939, 55));
    CHECK_EQ(b.percentForX(SliderWidget::TRACK_X0), 0);
    CHECK_EQ(b.percentForX(940 - SliderWidget::TRACK_PAD), 100);

    b.onDrag(SliderWidget::TRACK_X0, 30, DragPhase::Begin);
    CHECK_EQ(b.percent(), 0);
    b.onDrag(940 - SliderWidget::TRACK_PAD, 30, DragPhase::Move);
    CHECK_EQ(b.percent(), 100);
    b.onDrag(940 - SliderWidget::TRACK_PAD, 30, DragPhase::End);
    CHECK_EQ(b.percent(), 100);

    // Renders something real: sun pill + track + knob + readout.
    b.onTap(581, 30);  // ~60% (backend write is a silent no-op non-root)
    Framebuffer fb(940, 60);
    b.render(fb);
    int lit = 0;
    for (int i = 0; i < 940 * 60; ++i)
        if (fb.data()[i] != 0xFF000000) ++lit;
    CHECK(lit > 500);
}

// ---- mode registry + manager ----

static void test_modes() {
    EventLoop loop;
    Config cfg;
    Framebuffer composite(1000, 60);
    DumpOutput output("/tmp/tbfx-test-frames");
    LayoutEngine layout(1000, 60);
    Scheduler sched(loop, output, layout, composite, 16);

    std::unique_ptr<AudioSource> audio = std::make_unique<NullAudioSource>();
    ModeContext ctx{loop, cfg, audio, nullptr};
    ctx.mpris_factory = [] { return std::make_unique<FakeMprisClient>(); };

    // Unknown modes yield nothing.
    CHECK(createMode("bogus", ctx) == nullptr);

    // The media center self-registers: mode button + transport + viz +
    // volume. The sliders mode joins when a backlight device exists.
    cfg.mode_order = {"bogus", "mediacenter", "sliders"};
    ModeManager modes(loop, layout, sched, cfg, ctx);
    CHECK(modes.start());
    CHECK(modes.currentMode() == "mediacenter");
    CHECK(modes.handleCommand("mode bogus").rfind("err", 0) == 0);
    const bool have_sliders = BrightnessWidget::available();

    // Zone layout left-to-right: 60 mode | 50 preset | 200 transport |
    // flex viz | 300 volume slider.
    const auto& slots = layout.slots();
    CHECK(slots.size() == 5);
    if (slots.size() == 5) {
        CHECK_EQ(slots[0].x_offset, 0);
        CHECK_EQ(slots[0].width, 60);
        CHECK_EQ(slots[1].x_offset, 60);
        CHECK_EQ(slots[1].width, 50);
        CHECK_EQ(slots[2].x_offset, 110);
        CHECK_EQ(slots[2].width, 200);
        CHECK_EQ(slots[3].x_offset, 310);
        CHECK_EQ(slots[3].width, 1000 - 610);
        CHECK_EQ(slots[4].x_offset, 700);
        CHECK_EQ(slots[4].width, 300);
    }

    // The preset button (immediately right of the mode button) cycles
    // presets: waves -> dolphins -> oscilloscope -> penguin -> pioneer
    // -> spectrum -> waves.
    Widget* pbtn = layout.widgetAt(85);
    CHECK(pbtn != nullptr);
    if (pbtn) {
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok dolphins");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok oscilloscope");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok penguin");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok pioneer");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok spectrum");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok waves");
    }

    // Mode button: tap while home is a no-op (synthetic touch frames).
    TouchInput touch(loop, layout);
    touch.setAxisRangesForTest(0, 1000, 0, 60);  // raw == logical
    auto tap = [&](int x, int y) {
        touch.onAbsEvent(ABS_MT_TRACKING_ID, 1);
        touch.onAbsEvent(ABS_MT_POSITION_X, x);
        touch.onAbsEvent(ABS_MT_POSITION_Y, y);
        touch.onKeyEvent(BTN_TOUCH, 1);
        touch.onSynReport();
        touch.onAbsEvent(ABS_MT_TRACKING_ID, -1);
        touch.onKeyEvent(BTN_TOUCH, 0);
        touch.onSynReport();
    };
    tap(30, 30);
    CHECK(modes.currentMode() == "mediacenter");

    // A short hold is not a long-press; the release is a plain tap.
    touch.onAbsEvent(ABS_MT_TRACKING_ID, 1);
    touch.onAbsEvent(ABS_MT_POSITION_X, 30);
    touch.onAbsEvent(ABS_MT_POSITION_Y, 30);
    touch.onSynReport();
    usleep(100'000);
    touch.longPressCheck();  // too early — must not fire
    CHECK(modes.currentMode() == "mediacenter");
    touch.onAbsEvent(ABS_MT_TRACKING_ID, -1);
    touch.onSynReport();
    CHECK(modes.currentMode() == "mediacenter");

    if (have_sliders) {
        // Long-press (>= 600 ms within tap slop) enters sliders...
        touch.onAbsEvent(ABS_MT_TRACKING_ID, 1);
        touch.onAbsEvent(ABS_MT_POSITION_X, 30);
        touch.onAbsEvent(ABS_MT_POSITION_Y, 30);
        touch.onSynReport();
        usleep(700'000);
        touch.longPressCheck();
        CHECK(modes.currentMode() == "sliders");
        // ...and the release must NOT also fire a tap (which would
        // bounce straight back home).
        touch.onAbsEvent(ABS_MT_TRACKING_ID, -1);
        touch.onSynReport();
        CHECK(modes.currentMode() == "sliders");

        // In sliders the left edge is a mode button too; tap = home.
        Widget* sbtn = layout.widgetAt(30);
        CHECK(sbtn != nullptr);
        tap(30, 30);
        CHECK(modes.currentMode() == "mediacenter");
    }

    // Preset commands route through the manager to the media center.
    CHECK(modes.handleCommand("preset oscilloscope") == "ok oscilloscope");
    CHECK(modes.handleCommand("preset") == "ok oscilloscope");
    layout.render(composite);  // materializes the renderer
    CHECK(modes.handleCommand("preset next") == "ok penguin");
    CHECK(modes.handleCommand("preset prev") == "ok oscilloscope");
    CHECK(modes.handleCommand("preset bogus").rfind("err", 0) == 0);
    CHECK(modes.handleCommand("frobnicate").rfind("err", 0) == 0);

    // The hue command drives the shared pioneer color scheme, and a
    // retint bumps the theme generation so static widgets re-render.
    theme::apply(Theme::fromName("oel"), "oel");
    const uint32_t gen = theme::generation;
    CHECK(modes.handleCommand("hue 250 30") == "ok hue 250/30");
    CHECK(theme::generation > gen);
    CHECK(pioneerHues().main == 250.0f);
    CHECK(pioneerHues().accent == 30.0f);
    CHECK(manualHues().main == 250.0f);    // manual picks remembered
    CHECK(manualHues().accent == 30.0f);
    CHECK(modes.handleCommand("hue") == "ok main=250 accent=30");
    CHECK(modes.handleCommand("hue bogus").rfind("err", 0) == 0);
    CHECK(modes.handleCommand("hue 999").rfind("err", 0) == 0);
    pioneerHues().main = 190.0f;  // restore stock
    pioneerHues().accent = 12.0f;
    manualHues().main = 190.0f;
    manualHues().accent = 12.0f;
    theme::retint(190.0f);

    // The art command stores an RGB565 album thumbnail, decoded.
    CHECK(modes.handleCommand("art 2 2 fffff800001f07e0")
          == "ok art 2x2");
    CHECK(albumArt().w == 2 && albumArt().h == 2);
    CHECK(albumArt().px.size() == 4);
    CHECK(albumArt().px[0] == 0xFFFFFFFF);  // white
    CHECK(albumArt().px[1] == 0xFFFF0000);  // red
    CHECK(modes.handleCommand("art 2 2 ff").rfind("err", 0) == 0);
    CHECK(modes.handleCommand("art 99 2 ff").rfind("err", 0) == 0);
}

// ---- album cover tap: restore manual hues ----

static void test_cover_tap() {
    Config cfg;
    Framebuffer fb(1000, 60);
    VizWidget viz(std::make_unique<NullAudioSource>(), cfg);
    viz.setAllocatedWidth(1000);
    FakeMprisClient mpris;
    viz.setMpris(&mpris);

    // A track's art arrived: thumbnail in the corner, live scheme
    // recolored from the cover; the user's own picks sit in manual.
    AlbumArt& art = albumArt();
    art.w = art.h = 28;
    art.px.assign(28 * 28, 0xFFCC3300);
    ++art.seq;
    theme::apply(Theme::fromName("oel"), "oel");
    manualHues().main = 300.0f;
    manualHues().accent = 60.0f;
    artHues().main = 20.0f;
    artHues().accent = 200.0f;
    pioneerHues() = artHues();  // art-derived, live

    viz.render(fb);  // materializes the renderer + bar height

    // The cover tap toggles: art -> manual -> art -> manual.
    viz.onTap(30, 30);
    CHECK(pioneerHues().main == 300.0f);
    CHECK(pioneerHues().accent == 60.0f);
    viz.onTap(30, 30);
    CHECK(pioneerHues().main == 20.0f);
    CHECK(pioneerHues().accent == 200.0f);
    viz.onTap(30, 30);
    CHECK(pioneerHues().main == 300.0f);

    // Tap outside the cover -> forwarded to the preset, hues untouched.
    pioneerHues() = artHues();
    viz.onTap(500, 30);
    CHECK(pioneerHues().main == 20.0f);

    // No art -> the cover area falls through to the preset as before.
    art.px.clear();
    viz.onTap(30, 30);
    CHECK(pioneerHues().main == 20.0f);

    // Restore stock for later tests.
    art.w = art.h = 0;
    pioneerHues().main = 190.0f;
    pioneerHues().accent = 12.0f;
    manualHues().main = 190.0f;
    manualHues().accent = 12.0f;
    theme::retint(190.0f);
}

// ---- PulseAudio reconnect ----

static void test_audio_reconnect() {
    // Point at a dead server: the source must keep retrying (every
    // ~2 s) without crashing, and read() must just yield silence.
    setenv("PULSE_SERVER", "unix:/nonexistent/tbfx-test", 1);
    PulseMonitorSource src;
    src.start();
    int attempts = 0;
    for (int i = 0; i < 120 && attempts < 2; ++i) {
        usleep(50'000);
        attempts = src.connectAttempts();
    }
    src.stop();
    unsetenv("PULSE_SERVER");
    CHECK(attempts >= 2);

    float buf[64];
    CHECK(src.read(buf, 64) == 0);  // silence, no crash
}

// ---- Album art color analysis ----

static void test_art_analyze() {
    const int W = 28, H = 28;
    std::vector<uint8_t> rgb(W * H * 3);

    // Solid red: main hue ~0, no second mass and no white -> the
    // analogous fallback, never an invented complement.
    std::fill(rgb.begin(), rgb.end(), 0);
    for (int i = 0; i < W * H; ++i) rgb[i * 3] = 220;
    ArtAnalysis red = analyze_art(rgb.data(), W, H);
    CHECK(red.px.size() == static_cast<size_t>(W * H));
    CHECK(red.px[0] == 0xFFDC0000);
    CHECK(red.main_hue >= 350.0f || red.main_hue <= 10.0f);
    CHECK(red.accent_hue > 20.0f && red.accent_hue < 40.0f);

    // One hue + white line art (the Deftones cover shape): the white
    // is the accent, not a complement.
    for (int i = 0; i < W * H; ++i) {
        if (i % 3 == 0) {  // a third of the pixels white
            rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = 240;
        } else {
            rgb[i * 3] = 30; rgb[i * 3 + 1] = 200; rgb[i * 3 + 2] = 60;
        }
    }
    ArtAnalysis snake = analyze_art(rgb.data(), W, H);
    CHECK(snake.main_hue > 110.0f && snake.main_hue < 160.0f);  // green
    CHECK(snake.accent_hue == 396.0f);                          // white

    // Grayscale gradient: nobody votes -> white/grayscale mode (396).
    for (int i = 0; i < W * H; ++i) {
        const uint8_t v = static_cast<uint8_t>(40 + (i % W) * 6);
        rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = v;
    }
    ArtAnalysis gray = analyze_art(rgb.data(), W, H);
    CHECK(gray.main_hue == 396.0f);
    CHECK(gray.accent_hue == 396.0f);

    // Half red / half blue: the two masses come out ~120 deg apart
    // and either may win the primary slot.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            uint8_t* p = &rgb[(y * W + x) * 3];
            if (x < W / 2) { p[0] = 220; p[1] = 0; p[2] = 0; }
            else           { p[0] = 0; p[1] = 0; p[2] = 220; }
        }
    ArtAnalysis rb = analyze_art(rgb.data(), W, H);
    const bool red_first =
        (rb.main_hue <= 10.0f || rb.main_hue >= 350.0f) &&
        rb.accent_hue > 230.0f && rb.accent_hue < 250.0f;
    const bool blue_first =
        rb.main_hue > 230.0f && rb.main_hue < 250.0f &&
        (rb.accent_hue <= 10.0f || rb.accent_hue >= 350.0f);
    CHECK(red_first || blue_first);
}

int main() {
    test_gestures();
    test_volume_drag();
    test_tap_routing();
    test_first_touch_center();
    test_seek_drag();
    test_brightness_slider();
    test_presets();
    test_config();
    test_theme();
    test_modes();
    test_art_analyze();
    test_cover_tap();
    test_audio_reconnect();  // last: sets PULSE_SERVER

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
