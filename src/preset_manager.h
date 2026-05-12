#pragma once

#include "app_state.h"
#include <string>
#include <vector>

class PresetManager {
public:
    // Load presets from JSON file (or create defaults)
    static std::vector<Preset> LoadPresets(const std::string& path);
    static void SavePresets(const std::string& path,
                            const std::vector<Preset>& presets);

    // Apply preset to current RPC config
    static void ApplyPreset(const Preset& preset, AppState& state);

    // Create the 4 default templates
    static std::vector<Preset> CreateDefaults();
};
