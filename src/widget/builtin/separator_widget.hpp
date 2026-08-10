#pragma once
#include "widget/widget.hpp"

class SeparatorWidget : public Widget {
public:
    SeparatorWidget();
    void update() override {}
    void render(Framebuffer& fb) override;
};
