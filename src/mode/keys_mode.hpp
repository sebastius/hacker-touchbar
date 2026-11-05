#pragma once
#include "mode/mode.hpp"
#include "widget/builtin/key_row_widget.hpp"

// Full-bar rows of real keys. "fkeys" shows while Fn is held,
// "controls" is the everyday row a quick Fn tap keeps on screen.
class KeysMode : public Mode {
public:
    KeysMode(const char* name, std::vector<KeyRowWidget::Key> keys,
             KeyRowWidget::Sink sink)
        : name_(name), row_(std::move(keys), std::move(sink)) {}

    const char* name() const override { return name_; }
    std::vector<Widget*> widgets() override { return {&row_}; }

private:
    const char* name_;
    KeyRowWidget row_;
};
