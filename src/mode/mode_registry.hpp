#pragma once
#include "mode/mode.hpp"
#include <map>

using ModeFactory = std::function<std::unique_ptr<Mode>(ModeContext&)>;

// Global mode registry. Mode files self-register at static-init time
// via REGISTER_MODE; ModeManager instantiates the names listed in the
// TOML [modes] order, skipping unknown names with a warning.
bool registerModeFactory(const std::string& name, ModeFactory factory);

// nullptr when the name is not registered.
std::unique_ptr<Mode> createMode(const std::string& name, ModeContext& ctx);

#define REGISTER_MODE(NAME, CLASS)                                     \
    static const bool CLASS##_mode_registered =                        \
        registerModeFactory(                                           \
            NAME, [](ModeContext& ctx) { return std::make_unique<CLASS>(ctx); })
