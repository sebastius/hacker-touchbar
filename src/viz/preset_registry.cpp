#include "viz/preset_registry.hpp"
#include "viz/preset.hpp"
#include <map>

static std::map<std::string, PresetFactory>& registry() {
    static std::map<std::string, PresetFactory> reg;
    return reg;
}

bool registerPreset(const std::string& name, PresetFactory factory) {
    registry()[name] = std::move(factory);
    return true;
}

std::unique_ptr<VizPreset> createPreset(const std::string& name) {
    auto it = registry().find(name);
    if (it == registry().end()) return nullptr;
    return it->second();
}

std::vector<std::string> presetNames() {
    std::vector<std::string> names;
    for (auto& [name, _] : registry()) names.push_back(name);
    return names;
}
