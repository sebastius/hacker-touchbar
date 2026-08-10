#include "mode/mode_registry.hpp"

static std::map<std::string, ModeFactory>& registry() {
    static std::map<std::string, ModeFactory> reg;
    return reg;
}

bool registerModeFactory(const std::string& name, ModeFactory factory) {
    registry()[name] = std::move(factory);
    return true;
}

std::unique_ptr<Mode> createMode(const std::string& name, ModeContext& ctx) {
    auto it = registry().find(name);
    if (it == registry().end()) return nullptr;
    return it->second(ctx);
}
