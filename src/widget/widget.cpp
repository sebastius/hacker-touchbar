#include "widget/widget.hpp"
#include "engine/scheduler.hpp"

void Widget::markDirty() {
    dirty_ = true;
    if (scheduler_) scheduler_->markDirty();
}
