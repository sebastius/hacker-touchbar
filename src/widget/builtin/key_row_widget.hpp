#pragma once
#include "widget/widget.hpp"
#include <functional>
#include <string>
#include <vector>

// A full-width row of keys, used by the Fn layers. Real keys go down
// on touch and up on release, so holding repeats and combos work.
class KeyRowWidget : public Widget {
public:
    enum class Show { Label, Clock, Battery };
    struct Key {
        Show show;
        std::string label;
        std::vector<int> codes;        // key events to send
        int stretch = 1;
        std::function<void()> action;  // or run this on touch
        std::function<std::string()> text = nullptr;  // live label
    };
    using Sink = std::function<void(int code, bool down)>;

    KeyRowWidget(std::vector<Key> keys, Sink sink);

    void update() override;
    void render(Framebuffer& fb) override;
    void onPress(int x, int y) override;
    void onRelease() override;
    int timerIntervalMs() const override { return 1000; }

    // Index of the key under widget-local x, or -1.
    int keyAt(int x) const;
    int keyCount() const { return static_cast<int>(keys_.size()); }

private:
    void keySpan(int index, int width, int& x, int& w) const;
    std::string textOf(const Key& k) const;

    std::vector<Key> keys_;
    Sink sink_;
    int total_stretch_ = 0;
    int pressed_ = -1;
    std::string clock_;
    std::string battery_;
};
