#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

class VizPreset;

using PresetFactory = std::function<std::unique_ptr<VizPreset>()>;

// Global preset registry. Preset files self-register at static-init
// time via REGISTER_PRESET, so adding a preset is one new file (picked
// up by the CMake glob) plus the registration line inside it.
bool registerPreset(const std::string& name, PresetFactory factory);

// nullptr when the name is not registered.
std::unique_ptr<VizPreset> createPreset(const std::string& name);

// All registered preset names, sorted (rotation order).
std::vector<std::string> presetNames();

#define REGISTER_PRESET(NAME, CLASS)                                        \
    static const bool CLASS##_preset_registered =                           \
        registerPreset(NAME, [] { return std::make_unique<CLASS>(); })
