#include "widget/builtin/viz_widget.hpp"
#include "audio/audio_source.hpp"
#include "viz/viz_renderer.hpp"
#include "viz/album_art.hpp"
#include "viz/pioneer_hues.hpp"
#include "config/config.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include "util/log.hpp"
#include <algorithm>

VizWidget::VizWidget(std::unique_ptr<AudioSource> source, const Config& cfg)
    : source_(std::move(source)), cfg_(cfg)
{
    name_ = "visualizer";
    requested_width_ = 0;  // flexible — fills whatever the mode leaves
}

VizWidget::~VizWidget() {
    shutdown();
}

bool VizWidget::init() {
    next_rotate_ms_ = nowMs() + cfg_.viz_rotate_seconds * 1000;
    return source_->start();
}

void VizWidget::shutdown() {
    if (source_) source_->stop();
}

int64_t VizWidget::nowMs() const {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void VizWidget::update() {
    // MPRIS position interpolation (lightweight; resyncs every 2 s
    // inside the client).
    if (mpris_) mpris_->poll(timerIntervalMs());

    // Preset auto-rotate.
    if (viz_ && cfg_.viz_rotate_seconds > 0 && next_rotate_ms_ > 0 &&
        nowMs() >= next_rotate_ms_) {
        viz_->cyclePreset(+1);
        next_rotate_ms_ = nowMs() + cfg_.viz_rotate_seconds * 1000;
        log_info("Preset auto-rotate -> '%s'", viz_->presetName().c_str());
    }

    // Marquee scroll.
    if (mpris_ && mpris_->state().has_player) {
        const MprisState& st = mpris_->state();
        std::string text = st.artist.empty()
            ? st.title : st.artist + " - " + st.title;
        if (!text.empty() &&
            static_cast<int>(text.size()) * 8 > allocatedWidth() - 16)
            scroll_offset_++;
    }

    if (!viz_) return;
    // Drain roughly one frame's worth of audio
    size_t want = source_->sampleRate() / 60;
    buf_.resize(want * 2);
    size_t n = source_->read(buf_.data(), buf_.size());
    if (n > 0)
        viz_->feedSamples(buf_.data(), n, source_->sampleRate());
    markDirty();  // trails decay even without new audio
}

void VizWidget::ensureRenderer(int w, int h) {
    if (viz_) return;
    const std::string& initial =
        pending_preset_.empty() ? cfg_.viz_preset : pending_preset_;
    viz_ = std::make_unique<VizRenderer>(w, h, cfg_, initial);
    log_info("Visualizer renderer: %dx%d, preset '%s'",
             w, h, viz_->presetName().c_str());
}

// ---- presets ----

void VizWidget::onTap(int x, int y) {
    // Tap the album cover: toggle the bar between the track's
    // art-derived colors and the user's own picks (colors mode
    // sliders / "hue" command). A new track re-applies its art
    // colors via the fetcher.
    const AlbumArt& art = albumArt();
    if (mpris_ && mpris_->state().has_player && !art.px.empty()) {
        const int ah = bar_h_ - 4;
        const int aw = ah * art.w / art.h;
        if (x >= 2 && x <= 4 + aw + 2) {
            const bool on_art = pioneerHues().main == artHues().main
                                && pioneerHues().accent == artHues().accent;
            pioneerHues() = on_art ? manualHues() : artHues();
            theme::retint(pioneerHues().main);
            log_info("Art cover tap: %s hues %.0f/%.0f",
                     on_art ? "manual" : "art",
                     pioneerHues().main, pioneerHues().accent);
            return;  // not forwarded to the preset
        }
    }
    if (viz_) viz_->onTap(x, y);  // e.g. pioneer panel focus
}

void VizWidget::onLongPress(int x, int y) {
    if (viz_) viz_->onLongPress(x, y);  // e.g. pioneer page flip
}

bool VizWidget::setPreset(const std::string& name) {
    if (!viz_) {
        pending_preset_ = name;  // validated when the renderer is created
        return true;
    }
    if (!viz_->setPreset(name)) return false;
    // Manual switch pauses auto-rotate for 2x the interval.
    next_rotate_ms_ = nowMs() + 2 * cfg_.viz_rotate_seconds * 1000;
    return true;
}

void VizWidget::cyclePreset(int dir) {
    if (!viz_) return;
    viz_->cyclePreset(dir);
    next_rotate_ms_ = nowMs() + 2 * cfg_.viz_rotate_seconds * 1000;
}

std::string VizWidget::presetName() const {
    if (viz_) return viz_->presetName();
    return pending_preset_.empty() ? cfg_.viz_preset : pending_preset_;
}

// ---- track flash ----

void VizWidget::flashTrack(int seconds) {
    flash_until_ms_ = nowMs() + seconds * 1000;
    scroll_offset_ = 0;
    markDirty();
}

// ---- seek drag ----

// The digitizer never reports Y, so hit-testing is X-only: any
// horizontal drag on the center zone scrubs (the 3 px strip along the
// bottom edge visualizes the position).
bool VizWidget::dragCapableAt(int, int) const {
    if (!mpris_) return false;
    const MprisState& st = mpris_->state();
    return st.has_player && st.length_us > 0;
}

void VizWidget::onDrag(int x, int, DragPhase phase) {
    if (!mpris_ || phase == DragPhase::End) return;
    const MprisState& st = mpris_->state();
    if (!st.has_player || st.length_us <= 0) return;

    int64_t now = nowMs();
    if (phase == DragPhase::Move && now - last_seek_ms_ < SEEK_MIN_MS)
        return;
    last_seek_ms_ = now;

    int w = std::max(1, allocatedWidth());
    int clamped = std::max(0, std::min(w - 1, x));
    mpris_->seekTo(st.length_us * clamped / w);
}

// ---- render ----

void VizWidget::render(Framebuffer& fb) {
    bar_h_ = fb.height();  // cover-tap hit rect uses the same geometry
    ensureRenderer(fb.width(), fb.height());
    viz_->render(fb);
    drawOverlay(fb);
    drawSeekStrip(fb);
}

void VizWidget::drawOverlay(Framebuffer& fb) {
    if (!mpris_) return;
    const MprisState& st = mpris_->state();
    if (!st.has_player || (st.title.empty() && st.artist.empty())) return;

    int w = fb.width();
    bool flash = nowMs() < flash_until_ms_;

    // Album art thumbnail (filled by the MPRIS art fetcher on track
    // change): nearest-neighbor to bar height at the left, framed
    // like an OEL display window; text strips shift right.
    const AlbumArt& art = albumArt();
    int tx = 4;  // text strip left edge
    if (!art.px.empty()) {
        const int ah = fb.height() - 4;
        const int aw = ah * art.w / art.h;
        for (int y = 0; y < ah; ++y)
            for (int x = 0; x < aw; ++x)
                fb.setPixel(4 + x, 2 + y,
                            art.px[static_cast<size_t>(
                                (y * art.h / ah) * art.w
                                + x * art.w / aw)]);
        fb.drawRect(3, 1, aw + 2, ah + 2, theme::zone_border);
        tx = 4 + aw + 6;
    }

    std::string title = st.title.empty() ? "(untitled)" : st.title;
    std::string text = st.artist.empty() ? title
                                         : st.artist + " - " + title;
    int max_w = w - tx - 20;
    if (fb.textWidth(text, FontSize::Normal) > max_w && scroll_offset_ > 0) {
        int off = (scroll_offset_ / 3) % static_cast<int>(text.size() + 5);
        std::string padded = text + "     " + text;
        text = padded.substr(off);
    }

    // Dimmed backdrop strips sized to the text, so the visualizer
    // stays visible around them. Flash brightens both strip and text.
    uint32_t backdrop = flash ? theme::btn : theme::green_bg;
    int tw = std::min(fb.textWidth(text, FontSize::Normal), max_w);
    fb.fillRoundRect(tx, 2, tw + 16, 20, theme::corner_radius, backdrop);
    if (flash)
        fb.drawRoundRect(tx, 2, tw + 16, 20, theme::corner_radius,
                         theme::btn_border);
    fb.drawText(tx + 8, 4, text, flash ? theme::text : theme::text_dim,
                FontSize::Normal);

    // Second strip: the album when the player reports it, else the
    // title again in small type as before.
    const std::string& sub = !st.album.empty() ? st.album : st.title;
    if (!st.artist.empty() && !sub.empty()) {
        int aw = fb.textWidth(sub, FontSize::Small);
        fb.fillRoundRect(tx, 24, std::min(aw + 12, w - tx - 4), 14,
                         theme::corner_radius, backdrop);
        fb.drawText(tx + 6, 26, sub, theme::text_muted, FontSize::Small);
    }
}

void VizWidget::drawSeekStrip(Framebuffer& fb) {
    if (!mpris_) return;
    const MprisState& st = mpris_->state();
    if (!st.has_player || st.length_us <= 0) return;

    int w = fb.width();
    int y = fb.height() - SEEK_STRIP_H;
    fb.fillRect(0, y, w, SEEK_STRIP_H, theme::bar_bg);
    int64_t pos = std::min(st.position_us, st.length_us);
    int fill = static_cast<int>(pos * w / st.length_us);
    if (fill > 0)
        fb.fillRect(0, y, fill, SEEK_STRIP_H,
                    st.playing ? theme::green : theme::text_dim);
}
