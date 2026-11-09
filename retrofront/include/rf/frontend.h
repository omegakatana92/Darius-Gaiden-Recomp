// RetroFront - public entry point
#pragma once
#include "rf/adapter.h"
#include <functional>

namespace rf {

struct FrontendOptions {
    const char *default_profile_json = "{}";  // built-in profile; profiles/<game_id>.json overrides it
    const char *version = "1.0";
};

// Runs the complete frontend (menus, settings, saves, gadgets, display/audio/input) around one adapter.
int run_frontend(std::unique_ptr<GameAdapter> adapter, const FrontendOptions &opt, int argc, char **argv);

// Several selectable editions (or versions) of one game in one application. Each edition has a stable id,
// its own profile, adapter, content location, save states and native saves; display, audio, controller and
// interface settings are shared. Only one edition's adapter exists at a time: switching destroys the
// current adapter (after flushing its saves) and builds the next one from scratch.
struct EditionEntry {
    std::string id;            // stable identifier, e.g. "darius_gaiden_original" (folder + save tag)
    std::string label;         // short name, e.g. "Original"
    std::string long_label;    // e.g. "Darius Gaiden - Original"
    std::string detail;        // e.g. revision text
    const char *profile_json = "{}";
    std::function<std::unique_ptr<GameAdapter>()> make;
};
struct MultiOptions {
    std::string app_id, app_title;    // shared settings live in <pref>/RetroFront/<app_id>/
    const char *version = "1.0";
    std::vector<EditionEntry> editions;
};
int run_frontend_multi(const MultiOptions &opt, int argc, char **argv);

constexpr const char *kFrontendName = "RetroFront";
constexpr const char *kFrontendVersion = "1.0.0";
} // namespace rf
