#include "widget/widget_registry.hpp"
#include "widget/widget.hpp"
#include "util/log.hpp"

WidgetRegistry& WidgetRegistry::instance() {
    static WidgetRegistry reg;
    return reg;
}

void WidgetRegistry::registerType(const std::string& type, Factory factory) {
    factories_[type] = std::move(factory);
}

std::unique_ptr<Widget> WidgetRegistry::create(const std::string& type) const {
    auto it = factories_.find(type);
    if (it == factories_.end()) {
        log_error("Unknown widget type: %s", type.c_str());
        return nullptr;
    }
    return it->second();
}
