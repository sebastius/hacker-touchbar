#pragma once
#include "widget/widget.hpp"

class SpacerWidget : public Widget {
public:
    SpacerWidget();
    void update() override {}
    void render(Framebuffer& fb) override;
};
