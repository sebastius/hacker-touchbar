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
#include "widget/builtin/key_row_widget.hpp"
#include "input/fn_watcher.hpp"
#include "util/bar_backlight.hpp"
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
         has_spectrum = false;
    for (auto& n : names) {
        if (n == "waves") has_waves = true;
        if (n == "oscilloscope") has_scope = true;
        if (n == "dolphins") has_dolphins = true;
        if (n == "spectrum") has_spectrum = true;
    }
    CHECK(has_waves);
    CHECK(has_scope);
    CHECK(has_dolphins);
    CHECK(has_spectrum);
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

    // Unknown preset name keeps the current one. Rotation order is
    // alphabetical: dolphins -> oscilloscope ->
    // spectrum -> waves.
    CHECK(!viz_a.setPreset("does-not-exist"));
    CHECK(viz_a.presetName() == "waves");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "dolphins");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "life");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "matrix");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "oscilloscope");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "spectrum");
    viz_a.cyclePreset(+1);
    CHECK(viz_a.presetName() == "stars");
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

    // Terminal themes: distinct phosphors, square corners only while on.
    Theme grn = Theme::fromName("green");
    Theme amb = Theme::fromName("amber");
    CHECK(grn.terminal && amb.terminal && !def.terminal);
    CHECK(grn.text != amb.text);
    theme::apply(amb, "amber");
    CHECK(Framebuffer::square_corners);

    // crt(): one hue, darker odd rows, black stays black.
    Framebuffer fbuf(2, 2);
    fbuf.clear(0xFFFFFFFF);
    fbuf.setPixel(1, 0, 0xFF000000);
    theme::crt(fbuf);
    CHECK(fbuf.getPixel(0, 0) == amb.text);
    CHECK(fbuf.getPixel(1, 0) == 0xFF000000);
    CHECK((fbuf.getPixel(0, 1) & 0xFF0000) < (amb.text & 0xFF0000));

    theme::apply(def);
    CHECK(!Framebuffer::square_corners);
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
    // presets: waves -> dolphins -> oscilloscope
    // -> spectrum -> waves.
    Widget* pbtn = layout.widgetAt(85);
    CHECK(pbtn != nullptr);
    if (pbtn) {
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok dolphins");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok life");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok matrix");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok oscilloscope");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok spectrum");
        pbtn->onTap(25, 30);
        CHECK(modes.handleCommand("preset") == "ok stars");
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
    CHECK(modes.handleCommand("preset next") == "ok spectrum");
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

static void test_fn_tracker() {
    std::vector<std::string> log;
    FnTracker t;
    t.on_hold = [&](bool h) { log.push_back(h ? "hold" : "free"); };
    t.on_tap = [&] { log.push_back("tap"); };

    t.onKey(KEY_FN, 1, 1000);   // quick tap: tap fires before release
    t.onKey(KEY_FN, 2, 1050);   // autorepeat is ignored
    t.onKey(KEY_FN, 0, 1100);
    CHECK((log == std::vector<std::string>{"hold", "tap", "free"}));

    log.clear();                // long hold is not a tap
    t.onKey(KEY_FN, 1, 2000);
    t.onKey(KEY_FN, 0, 2900);
    CHECK((log == std::vector<std::string>{"hold", "free"}));

    log.clear();                // Fn+arrow chord is not a tap
    t.onKey(KEY_FN, 1, 3000);
    t.onKey(KEY_LEFT, 1, 3010);
    t.onKey(KEY_LEFT, 0, 3020);
    t.onKey(KEY_FN, 0, 3030);
    CHECK((log == std::vector<std::string>{"hold", "free"}));

    log.clear();                // Fn + bar key is not a tap
    t.onKey(KEY_FN, 1, 4000);
    t.markUsed();
    t.onKey(KEY_FN, 0, 4030);
    CHECK((log == std::vector<std::string>{"hold", "free"}));
}

static void test_key_row() {
    using K = KeyRowWidget::Key;
    using G = KeyRowWidget::Show;
    std::vector<std::pair<int, bool>> sent;
    KeyRowWidget row({{G::Label, "A", {KEY_A}, 1, nullptr},
                      {G::Label, "AB", {KEY_LEFTCTRL, KEY_T}, 2, nullptr},
                      {G::Clock, "", {}, 1, nullptr}},
                     [&](int c, bool d) { sent.push_back({c, d}); });
    row.setAllocatedWidth(400);
    CHECK(row.keyAt(50) == 0);
    CHECK(row.keyAt(150) == 1);
    CHECK(row.keyAt(350) == 2);
    CHECK(row.keyAt(399) == 2);
    CHECK(row.keyAt(400) == -1);

    row.onPress(150, 30);       // combo: down in order, up reversed
    row.onRelease();
    CHECK((sent == std::vector<std::pair<int, bool>>{
        {KEY_LEFTCTRL, true}, {KEY_T, true},
        {KEY_T, false}, {KEY_LEFTCTRL, false}}));

    sent.clear();               // display-only key sends nothing
    row.onPress(350, 30);
    row.onRelease();
    CHECK(sent.empty());

    Framebuffer fb(400, 60);    // renders something on every key
    row.render(fb);
    int lit = 0;
    for (int i = 0; i < 400 * 60; ++i)
        if (fb.data()[i] != theme::bg) ++lit;
    CHECK(lit > 500);
    (void)sizeof(K);
}

static void test_calm_presets() {
    Config cfg;
    for (const char* name : {"matrix", "life", "stars"}) {
        Framebuffer fb(1500, 60);
        NullAudioSource src;
        VizRenderer viz(1500, 60, cfg, name);
        CHECK(viz.presetName() == name);
        renderFrames(viz, src, fb, 400);
        std::vector<uint32_t> before(fb.data(), fb.data() + 1500 * 60);
        int lit = 0;
        for (uint32_t c : before) lit += c != 0xFF000000;
        renderFrames(viz, src, fb, 30);
        int moved = 0;
        for (int i = 0; i < 1500 * 60; ++i) moved += fb.data()[i] != before[i];
        printf("%s: %d lit, %d changed in 30 frames\n", name, lit, moved);
        CHECK(lit > 150);
        CHECK(moved > 50);
    }
}

static void test_deck() {
    EventLoop loop;
    Config cfg;
    std::unique_ptr<AudioSource> src = std::make_unique<NullAudioSource>();
    FakeMprisClient* fake = nullptr;
    ModeContext ctx{loop, cfg, src, nullptr};
    ctx.mpris_factory = [&fake] {
        auto m = std::make_unique<FakeMprisClient>();
        fake = m.get();
        return m;
    };
    std::unique_ptr<Mode> deck = createMode("deck", ctx);
    CHECK(deck != nullptr);
    if (!deck) return;
    auto widgets = deck->widgets();
    CHECK_EQ(widgets.size(), 3u);
    CHECK(widgets[0]->name() == "playpause");

    CHECK(deck->handleCommand("preset") == "ok " + cfg.viz_preset);
    CHECK(deck->handleCommand("preset matrix") == "ok matrix");

    // Long press walks the cycle and wraps. Only presets from the list.
    auto* viz = dynamic_cast<VizWidget*>(widgets[2]);
    CHECK(viz != nullptr);
    if (!viz) return;
    Framebuffer fb(600, 60);
    viz->render(fb);  // creates the renderer
    deck->handleCommand("preset matrix");
    viz->onLongPress(10, 30);
    CHECK(viz->presetName() == "life");
    viz->onLongPress(10, 30);
    CHECK(viz->presetName() == "stars");
    viz->onLongPress(10, 30);
    CHECK(viz->presetName() == "matrix");
}

static void test_controls_config() {
    const char* path = "/tmp/tbfx-test-controls.toml";
    FILE* f = fopen(path, "w");
    CHECK(f != nullptr);
    if (!f) return;
    fputs("[[controls]]\n"
          "label = \"A\"\nkeys = [\"leftctrl\", \"A\"]\nwidth = 3\n"
          "[[controls]]\n"
          "label = \"BAD\"\nkeys = [\"NOSUCHKEY\"]\n"
          "[[controls]]\n"
          "show = \"nonsense\"\n"
          "[[controls]]\n"
          "label = \"NONE\"\n"
          "[[controls]]\n"
          "show = \"bar\"\n"
          "[[controls]]\n"
          "show = \"clock\"\n", f);
    fclose(f);
    Config cfg = Config::load(path);
    CHECK_EQ(cfg.controls.size(), 6u);
    CHECK_EQ(cfg.controls[0].width, 3);
    CHECK_EQ(cfg.controls[0].keys.size(), 2u);

    EventLoop loop;
    std::unique_ptr<AudioSource> src;
    ModeContext ctx{loop, cfg, src, nullptr};
    std::vector<std::pair<int, bool>> sent;
    ctx.key_sink = [&](int c, bool d) { sent.push_back({c, d}); };
    int level = 2, cycles = 0;
    ctx.bar_level = [&] { return level; };
    ctx.cycle_bar = [&] { ++cycles; level = BarBacklight::nextLevel(level); };

    std::unique_ptr<Mode> mode = createMode("controls", ctx);
    CHECK(mode != nullptr);
    if (!mode) return;
    auto* row = dynamic_cast<KeyRowWidget*>(mode->widgets()[0]);
    CHECK(row != nullptr);
    if (!row) return;
    CHECK_EQ(row->keyCount(), 3);  // the three bad entries are skipped

    row->setAllocatedWidth(500);   // A (56 px), BAR, open space, clock
    row->onPress(50, 30);          // lowercase names resolve too
    row->onRelease();
    CHECK((sent == std::vector<std::pair<int, bool>>{
        {KEY_LEFTCTRL, true}, {KEY_A, true},
        {KEY_A, false}, {KEY_LEFTCTRL, false}}));

    row->onPress(100, 30);         // BAR: full -> dim -> full, never off
    row->onRelease();
    CHECK_EQ(level, 1);
    row->onPress(100, 30);
    row->onRelease();
    CHECK_EQ(level, 2);
    CHECK_EQ(cycles, 2);
    CHECK_EQ(BarBacklight::nextLevel(0), 2);

    Config def;                    // the shipped row builds without skips
    ModeContext ctx2{loop, def, src, nullptr};
    std::unique_ptr<Mode> m2 = createMode("controls", ctx2);
    auto* row2 = dynamic_cast<KeyRowWidget*>(m2->widgets()[0]);
    CHECK(row2 && row2->keyCount() == static_cast<int>(def.controls.size()));
}

static void test_control_row_fit() {
    Config cfg;
    EventLoop loop;
    std::unique_ptr<AudioSource> src;
    ModeContext ctx{loop, cfg, src, nullptr};
    std::unique_ptr<Mode> mode = createMode("controls", ctx);
    auto* row = dynamic_cast<KeyRowWidget*>(mode->widgets()[0]);
    CHECK(row != nullptr);
    if (!row) return;
    row->setAllocatedWidth(2008);
    const int n = row->keyCount();

    // Smiley keys are narrow, and there is open space before the clock.
    CHECK_EQ(row->keyAt(20), 0);
    CHECK_EQ(row->keyAt(70), 1);
    CHECK_EQ(row->keyAt(1500), -1);
    CHECK_EQ(row->keyAt(2000), n - 1);  // battery sits at the right edge

    // Too narrow for its content: falls back to sharing the width.
    row->setAllocatedWidth(600);
    CHECK_EQ(row->keyAt(599), n - 1);
    CHECK(row->keyAt(300) >= 0);

    // The smileys are real glyphs, distinct from each other and from '?'.
    Framebuffer a(8, 16), b(8, 16), q(8, 16);
    a.clear(0); b.clear(0); q.clear(0);
    a.drawText(0, 0, "\x01", 0xFFFFFFFF);
    b.drawText(0, 0, "\x02", 0xFFFFFFFF);
    q.drawText(0, 0, "?", 0xFFFFFFFF);
    CHECK(memcmp(a.data(), b.data(), 8 * 16 * 4) != 0);
    CHECK(memcmp(a.data(), q.data(), 8 * 16 * 4) != 0);
    Framebuffer bar(8, 16);
    bar.clear(0);
    bar.drawText(0, 0, "\x16", 0xFFFFFFFF);
    CHECK(bar.getPixel(3, 9) != 0);   // thick bar, four rows
    CHECK(bar.getPixel(3, 5) == 0);
    CHECK(memcmp(bar.data(), q.data(), 8 * 16 * 4) != 0);
    Framebuffer dot(8, 16);
    dot.clear(0);
    dot.drawText(0, 0, "\x07", 0xFFFFFFFF);
    CHECK(dot.getPixel(3, 7) != 0);  // bullet, centered
    CHECK(dot.getPixel(0, 7) == 0);
    CHECK(memcmp(dot.data(), q.data(), 8 * 16 * 4) != 0);

    Framebuffer n1(8, 16), n2(8, 16);
    n1.clear(0);
    n2.clear(0);
    n1.drawText(0, 0, "\x0d", 0xFFFFFFFF);
    n2.drawText(0, 0, "\x0e", 0xFFFFFFFF);
    CHECK(memcmp(n1.data(), n2.data(), 8 * 16 * 4) != 0);
    CHECK(memcmp(n1.data(), q.data(), 8 * 16 * 4) != 0);

    // TOML escapes reach the label untouched.
    const char* path = "/tmp/tbfx-test-smiley.toml";
    FILE* f = fopen(path, "w");
    CHECK(f != nullptr);
    if (!f) return;
    fputs("[ui]\nkey_gap = 99\n"
          "[[controls]]\nlabel = \"\\u0001\"\nkeys = [\"A\"]\nblink = true\n", f);
    fclose(f);
    Config loaded = Config::load(path);
    CHECK(loaded.controls[0].label == "\x01");
    CHECK(loaded.controls[0].blink);
    CHECK_EQ(loaded.key_gap, 40);  // clamped
}

static void test_control_row_spacing() {
    using K = KeyRowWidget::Key;
    using G = KeyRowWidget::Show;
    auto sink = [](int, bool) {};
    K a{G::Label, "A", {KEY_A}, 1, nullptr};
    K gap{G::Gap, "", {}, 1, nullptr};
    gap.min_px = 100;
    K b{G::Label, "B", {KEY_B}, 1, nullptr};

    // A gap pushes the next key along and takes no input.
    std::vector<std::pair<int, bool>> sent;
    KeyRowWidget row({a, gap, b}, [&](int c, bool d) { sent.push_back({c, d}); }, true, 4);
    row.setAllocatedWidth(600);
    CHECK_EQ(row.keyAt(20), 0);
    CHECK_EQ(row.keyAt(100), 1);
    row.onPress(100, 30);
    row.onRelease();
    CHECK(sent.empty());
    CHECK_EQ(row.keyAt(180), 2);

    // key_gap widens the space between all keys.
    KeyRowWidget tight({a, b}, sink, true, 0), wide({a, b}, sink, true, 40);
    tight.setAllocatedWidth(600);
    wide.setAllocatedWidth(600);
    CHECK_EQ(tight.keyAt(60), 1);
    CHECK_EQ(wide.keyAt(60), 0);

    // Only the last character blinks, every second 250 ms tick.
    K cursor{G::Label, ">_", {KEY_T}, 1, nullptr};
    cursor.blink = true;
    KeyRowWidget blink({cursor}, sink, true, 4);
    blink.setAllocatedWidth(200);
    Framebuffer on(200, 60), off(200, 60);
    blink.render(on);
    blink.update();
    blink.clearDirty();
    blink.update();
    CHECK(blink.isDirty());
    blink.render(off);
    int diff = 0;
    for (int i = 0; i < 200 * 60; ++i) diff += on.data()[i] != off.data()[i];
    printf("cursor blink: %d pixels change\n", diff);
    CHECK(diff > 0);
    CHECK(diff < 150);  // the prompt itself stays put
}

static void test_key_background() {
    using K = KeyRowWidget::Key;
    using G = KeyRowWidget::Show;
    Config cfg;
    K key{G::Label, "A", {KEY_A}, 1, nullptr};
    KeyRowWidget row({key}, [](int, bool) {}, true, 4);
    CHECK_EQ(row.timerIntervalMs(), 250);
    row.setBackground(cfg, "matrix");
    CHECK_EQ(row.timerIntervalMs(), 16);
    row.setAllocatedWidth(800);

    Framebuffer fb(800, 60);
    for (int i = 0; i < 300; ++i) row.render(fb);
    std::vector<uint32_t> before(fb.data(), fb.data() + 800 * 60);
    for (int i = 0; i < 30; ++i) row.render(fb);
    int moved = 0, outside = 0;
    for (int y = 0; y < 60; ++y)
        for (int x = 0; x < 800; ++x) {
            const bool diff = fb.getPixel(x, y) != before[y * 800 + x];
            moved += diff;
            outside += diff && x > 100;  // right of the only key
        }
    printf("key background: %d pixels moved, %d beside the key\n", moved, outside);
    CHECK(outside > 50);

    row.clearDirty();  // a running background keeps asking for frames
    row.update();
    CHECK(row.isDirty());
}

static void test_controls_as_home() {
    Config cfg;
    cfg.mode_order = {"controls"};
    EventLoop loop;
    Framebuffer composite(2008, 60);
    DumpOutput output("/tmp/tbfx-test-frames");
    LayoutEngine layout(2008, 60);
    Scheduler sched(loop, output, layout, composite, 16);
    std::unique_ptr<AudioSource> src = std::make_unique<NullAudioSource>();
    FakeMprisClient* fake = nullptr;
    ModeContext ctx{loop, cfg, src, nullptr};
    ctx.mpris_factory = [&fake] {
        auto m = std::make_unique<FakeMprisClient>();
        fake = m.get();
        return m;
    };
    ModeManager modes(loop, layout, sched, cfg, ctx);
    CHECK(modes.start());
    CHECK(modes.currentMode() == "controls");
    CHECK(src != nullptr);  // no visualizer took the audio

    // Hold Fn for the F keys, release to come back. A tap changes nothing.
    modes.setFnHeld(true);
    CHECK(modes.currentMode() == "fkeys");
    modes.setFnHeld(false);
    CHECK(modes.currentMode() == "controls");
    modes.setFnHeld(true);
    modes.toggleControls();
    modes.setFnHeld(false);
    CHECK(modes.currentMode() == "controls");
    modes.setFnHeld(true);
    modes.setFnHeld(false);
    CHECK(modes.currentMode() == "controls");

    // The PLAY/PAUSE key still has a player to watch.
    CHECK(fake != nullptr);
    CHECK(ctx.is_playing != nullptr);
    if (!fake || !ctx.is_playing) return;
    const bool before = ctx.is_playing();
    fake->playPause();
    CHECK(ctx.is_playing() != before);
}

static void test_bar_key_look() {
    Config cfg;
    cfg.controls = {{"bar", "", {}, 0}};
    EventLoop loop;
    std::unique_ptr<AudioSource> src;
    ModeContext ctx{loop, cfg, src, nullptr};
    int level = 1;
    ctx.bar_level = [&] { return level; };
    std::unique_ptr<Mode> mode = createMode("controls", ctx);
    auto* row = dynamic_cast<KeyRowWidget*>(mode->widgets()[0]);
    CHECK(row != nullptr);
    if (!row) return;
    row->setAllocatedWidth(100);
    auto lit = [&] {
        Framebuffer fb(100, 60);
        row->render(fb);
        int n = 0;
        for (int i = 0; i < 100 * 60; ++i) n += fb.data()[i] == theme::text;
        return n;
    };
    const int dim = lit();
    level = 2;
    CHECK(lit() > dim);  // thick bar lights more than the underscore
}

static void test_control_row_look() {
    Config cfg;
    EventLoop loop;
    std::unique_ptr<AudioSource> src;
    ModeContext ctx{loop, cfg, src, nullptr};
    bool playing = false;
    ctx.is_playing = [&] { return playing; };
    std::unique_ptr<Mode> mode = createMode("controls", ctx);
    auto* row = dynamic_cast<KeyRowWidget*>(mode->widgets()[0]);
    CHECK(row != nullptr);
    if (!row) return;
    row->setAllocatedWidth(2008);

    // The play key's label follows the player.
    Framebuffer paused(2008, 60), active(2008, 60);
    row->render(paused);
    playing = true;
    row->clearDirty();
    row->update();
    CHECK(row->isDirty());
    row->render(active);
    CHECK(memcmp(paused.data(), active.data(), 2008 * 60 * 4) != 0);

    // The theme dot keeps the other theme's color through the CRT pass.
    Theme amb = Theme::fromName("amber");
    Theme grn = Theme::fromName("green");
    theme::apply(amb, "amber");
    Framebuffer fb(2008, 60);
    row->render(fb);
    theme::crt(fb);
    row->renderAfterCrt(fb);
    int dot = 0, wrong = 0;
    for (int i = 0; i < 2008 * 60; ++i) {
        uint32_t px = fb.data()[i];
        const bool greenish = ((px >> 8) & 0xFF) > ((px >> 16) & 0xFF);
        if (px == grn.text) ++dot;
        else if (greenish) ++wrong;
    }
    printf("theme dot: %d bright pixels\n", dot);
    CHECK(dot > 200);
    theme::apply(Theme::fromName("default"));
    (void)wrong;
}

int main() {
    test_gestures();
    test_volume_drag();
    test_tap_routing();
    test_first_touch_center();
    test_seek_drag();
    test_brightness_slider();
    test_presets();
    test_fn_tracker();
    test_key_row();
    test_calm_presets();
    test_deck();
    test_controls_config();
    test_control_row_fit();
    test_control_row_spacing();
    test_key_background();
    test_controls_as_home();
    test_bar_key_look();
    test_control_row_look();
    test_config();
    test_theme();
    test_modes();
    test_art_analyze();
    test_cover_tap();
    test_audio_reconnect();  // last: sets PULSE_SERVER

    printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
