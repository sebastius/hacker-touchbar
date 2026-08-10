#pragma once
#include <cstdint>
#include <vector>
#include <memory>

class Widget;
class Framebuffer;
class Scheduler;

class LayoutEngine {
public:
    LayoutEngine(int total_width, int total_height);

    void setWidgets(std::vector<std::unique_ptr<Widget>> widgets, Scheduler& scheduler);

    // Show already-initialized widgets owned elsewhere (used by
    // ModeManager to swap exclusive full-bar modes without
    // re-initializing their widgets).
    void setWidgetsBorrowed(const std::vector<Widget*>& widgets, Scheduler& scheduler);

    void render(Framebuffer& output);

    Widget* widgetAt(int x) const;
    const std::vector<std::unique_ptr<Widget>>& widgets() const { return widgets_; }

    int width() const { return total_width_; }
    int height() const { return total_height_; }

    struct Slot {
        Widget* widget;
        int x_offset;
        int width;
    };
    const std::vector<Slot>& slots() const { return slots_; }

private:
    void calculateLayout();

    int total_width_, total_height_;
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::vector<Widget*> borrowed_;  // non-owning, when set via setWidgetsBorrowed
    std::vector<Slot> slots_;
    bool force_redraw_ = true;
    uint32_t last_theme_gen_ = 0;
};
