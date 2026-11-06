#pragma once
#include "widget/widget.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

class VizRenderer;
struct Config;

// A full-width row of keys, used by the Fn layers. Real keys go down
// on touch and up on release, so holding repeats and combos work.
class KeyRowWidget : public Widget {
public:
    ~KeyRowWidget() override;

    enum class Show { Label, Clock, Battery, ThemeDot, Gap };
    struct Key {
        Show show;
        std::string label;
        std::vector<int> codes;        // key events to send
        int stretch = 1;
        std::function<void()> action;  // or run this on touch
        std::function<std::string()> text = nullptr;  // live label
        int min_px = 0;        // fit rows: never narrower than this
        std::string fit_text;  // fit rows: widest text, so keys don't jump
        bool blink = false;    // last character blinks, like a cursor
    };
    using Sink = std::function<void(int code, bool down)>;

    // A fit row sizes each key to its content, packs them to the left
    // and puts trailing display-only keys on the right. Otherwise keys
    // share the full width by stretch.
    KeyRowWidget(std::vector<Key> keys, Sink sink, bool fit = false,
                 int gap = 4);

    // Run a preset across the whole row, behind the keys.
    void setBackground(const Config& cfg, const std::string& preset);

    void update() override;
    void render(Framebuffer& fb) override;
    void renderAfterCrt(Framebuffer& fb) override;
    void onPress(int x, int y) override;
    void onRelease() override;
    int timerIntervalMs() const override { return bg_preset_.empty() ? 250 : 16; }

    // Index of the key under widget-local x, or -1.
    int keyAt(int x) const;
    int keyCount() const { return static_cast<int>(keys_.size()); }

private:
    void keySpan(int index, int width, int& x, int& w) const;
    int fitWidth(const Key& k) const;
    bool fits(int width) const;
    std::string textOf(const Key& k) const;

    std::vector<Key> keys_;
    Sink sink_;
    bool fit_ = false;
    int gap_ = 4;  // pixels between keys
    bool has_blink_ = false;
    bool blink_on_ = true;
    int total_stretch_ = 0;
    int pressed_ = -1;
    std::string clock_;
    std::string battery_;
    std::string live_labels_;
    int elapsed_ms_ = 0;

    const Config* bg_cfg_ = nullptr;
    std::string bg_preset_;
    std::unique_ptr<VizRenderer> bg_;
};
