#include "config/config.hpp"
#include "util/log.hpp"
#include "toml.hpp"
#include <cstdlib>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

std::string Config::defaultPath() {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return std::string(xdg) + "/touchbar-fx/config.toml";
    // Root under sudo: read the invoking user's config — that's the file
    // they actually edit, not /root/.config.
    if (getuid() == 0) {
        if (const char* sudo_uid = getenv("SUDO_UID")) {
            if (struct passwd* pw = getpwuid(static_cast<uid_t>(atoi(sudo_uid))))
                if (pw->pw_dir && *pw->pw_dir)
                    return std::string(pw->pw_dir) + "/.config/touchbar-fx/config.toml";
        }
    }
    return std::string(getenv("HOME")) + "/.config/touchbar-fx/config.toml";
}

Config Config::load(const std::string& path) {
    Config cfg;

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        log_info("No config at %s — using defaults", path.c_str());
        return cfg;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& err) {
        log_error("Config parse error in %s: %s — using defaults", path.c_str(), err.what());
        return cfg;
    }
    log_info("Loaded config from %s", path.c_str());

    if (auto* order = tbl["modes"]["order"].as_array()) {
        cfg.mode_order.clear();
        for (auto& m : *order) {
            if (auto s = m.value<std::string>()) cfg.mode_order.push_back(*s);
        }
        if (cfg.mode_order.empty())
            cfg.mode_order = {"mediacenter", "sliders"};
    }

    cfg.ui_theme           = tbl["ui"]["theme"].value_or(cfg.ui_theme);

    cfg.mc_transport_width = tbl["mediacenter"]["transport_width"].value_or(cfg.mc_transport_width);
    cfg.mc_volume_width    = tbl["mediacenter"]["volume_width"].value_or(cfg.mc_volume_width);
    cfg.mc_flash_seconds   = tbl["mediacenter"]["flash_seconds"].value_or(cfg.mc_flash_seconds);

    cfg.viz_preset         = tbl["visualizer"]["preset"].value_or(cfg.viz_preset);
    cfg.viz_rotate_seconds = tbl["visualizer"]["rotate_seconds"].value_or(cfg.viz_rotate_seconds);
    cfg.viz_bars        = tbl["visualizer"]["bars"].value_or(cfg.viz_bars);
    cfg.viz_smoothing   = tbl["visualizer"]["smoothing"].value_or(cfg.viz_smoothing);
    cfg.viz_base_hue    = tbl["visualizer"]["base_hue"].value_or(cfg.viz_base_hue);
    cfg.viz_hue_speed   = tbl["visualizer"]["hue_speed"].value_or(cfg.viz_hue_speed);
    cfg.viz_trail_decay = tbl["visualizer"]["trail_decay"].value_or(cfg.viz_trail_decay);
    cfg.viz_trail_zoom  = tbl["visualizer"]["trail_zoom"].value_or(cfg.viz_trail_zoom);
    cfg.viz_trail_drift = tbl["visualizer"]["trail_drift"].value_or(cfg.viz_trail_drift);

    cfg.volume_max_percent = tbl["volume"]["max_percent"].value_or(cfg.volume_max_percent);

    cfg.dump_width  = tbl["dump"]["width"].value_or(cfg.dump_width);
    cfg.dump_height = tbl["dump"]["height"].value_or(cfg.dump_height);

    return cfg;
}
