#include "widget/layout_engine.hpp"
#include "widget/widget.hpp"
#include "gfx/framebuffer.hpp"
#include "gfx/theme.hpp"
#include "engine/scheduler.hpp"

LayoutEngine::LayoutEngine(int total_width, int total_height)
    : total_width_(total_width), total_height_(total_height)
{}

void LayoutEngine::setWidgets(std::vector<std::unique_ptr<Widget>> widgets,
                              Scheduler& scheduler) {
    // Shut down old widgets
    for (auto& w : widgets_) w->shutdown();
    widgets_ = std::move(widgets);
    borrowed_.clear();

    for (auto& w : widgets_) {
        w->setScheduler(&scheduler);
        w->init();
    }

    force_redraw_ = true;
    calculateLayout();
}

void LayoutEngine::setWidgetsBorrowed(const std::vector<Widget*>& widgets,
                                      Scheduler& scheduler) {
    widgets_.clear();
    borrowed_ = widgets;
    for (auto* w : borrowed_)
        w->setScheduler(&scheduler);
    force_redraw_ = true;
    calculateLayout();
}

void LayoutEngine::calculateLayout() {
    slots_.clear();
    int fixed_total = 0;
    int flex_count = 0;

    std::vector<Widget*> active;
    if (!borrowed_.empty()) {
        active = borrowed_;
    } else {
        for (auto& w : widgets_) active.push_back(w.get());
    }

    for (auto* w : active) {
        if (w->requestedWidth() > 0) {
            fixed_total += w->requestedWidth();
        } else {
            flex_count++;
        }
    }

    int remaining = total_width_ - fixed_total;
    int flex_width = (flex_count > 0) ? (remaining / flex_count) : 0;
    if (flex_width < 0) flex_width = 0;

    int x = 0;
    for (auto* w : active) {
        int width = (w->requestedWidth() > 0) ? w->requestedWidth() : flex_width;
        w->setAllocatedWidth(width);
        slots_.push_back({w, x, width});
        x += width;
    }
}

void LayoutEngine::render(Framebuffer& output) {
    // A theme retint recolors every widget, including the static ones
    // that never mark themselves dirty — force a full redraw.
    if (theme::generation != last_theme_gen_) {
        last_theme_gen_ = theme::generation;
        force_redraw_ = true;
    }
    for (auto& slot : slots_) {
        if (!force_redraw_ && !slot.widget->isDirty()) continue;
        slot.widget->clearDirty();

        // Create a sub-framebuffer for this widget
        Framebuffer sub(slot.width, total_height_);
        sub.clear(0xFF000000);
        slot.widget->render(sub);
        if (theme::terminal) theme::crt(sub);

        // Blit into composite at the correct offset
        output.blit(sub, 0, 0, slot.width, total_height_, slot.x_offset, 0);

        // OEL theme: 1px display-window frame around the zone
        if (theme::zone_frame)
            output.drawRect(slot.x_offset, 0, slot.width, total_height_,
                            theme::zone_border);
    }
    force_redraw_ = false;
}

Widget* LayoutEngine::widgetAt(int x) const {
    for (auto& slot : slots_) {
        if (x >= slot.x_offset && x < slot.x_offset + slot.width) {
            return slot.widget;
        }
    }
    return nullptr;
}
