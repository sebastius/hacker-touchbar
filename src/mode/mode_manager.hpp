#pragma once
#include "mode/mode.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class EventLoop;
class LayoutEngine;
class Scheduler;
struct Config;
struct ModeContext;

// Exclusive full-bar modes: one mode owns the whole touch bar at a
// time. Modes come from the global mode registry (REGISTER_MODE) and
// are instantiated in the TOML [modes] order; unknown names are
// skipped with a warning. A horizontal swipe (or the `cycle` IPC
// command) steps through the order. When the MPRIS track changes or
// playback (re)starts, the manager jumps to the home mode (first in
// the order) and flashes its track overlay.
class ModeManager {
public:
    ModeManager(EventLoop& loop, LayoutEngine& layout, Scheduler& sched,
                const Config& cfg, ModeContext& ctx);

    // Enter the home mode (first in the configured order).
    bool start();

    bool setMode(const std::string& name);
    void cycle(int dir);  // +1 / -1 along the configured order

    // Auto-jump home on MPRIS track change / playback start.
    void notifyTrackChanged();

    const std::string& currentMode() const { return current_; }

    // IPC command handler: "cycle", "mode <name>"; anything else is
    // offered to each mode in order ("preset next", ...).
    std::string handleCommand(const std::string& cmd);

private:
    EventLoop& loop_;
    LayoutEngine& layout_;
    Scheduler& sched_;

    std::vector<std::string> order_;  // configured order (known modes only)
    std::unordered_map<std::string, std::unique_ptr<Mode>> modes_;
    std::string current_;
};
