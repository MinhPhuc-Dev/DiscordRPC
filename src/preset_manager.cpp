#include "preset_manager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── JSON serialization helpers ──────────────────────────────
static json ConfigToJson(const RPCConfig& c) {
    json j;
    j["clientId"]       = c.clientId;
    j["details"]        = c.details;
    j["state"]          = c.state;
    j["largeImageKey"]  = c.largeImageKey;
    j["largeImageText"] = c.largeImageText;
    j["smallImageKey"]  = c.smallImageKey;
    j["smallImageText"] = c.smallImageText;
    j["musicMode"]      = c.musicMode;
    j["useJoinButtons"] = c.useJoinButtons;

    json btns = json::array();
    for (int i = 0; i < 2; ++i) {
        if (!c.buttons[i].label.empty())
            btns.push_back({{"label", c.buttons[i].label},
                            {"url",   c.buttons[i].url}});
    }
    j["buttons"] = btns;
    return j;
}

static RPCConfig JsonToConfig(const json& j) {
    RPCConfig c;
    c.clientId       = j.value("clientId",       "");
    c.details        = j.value("details",        "");
    c.state          = j.value("state",          "");
    c.largeImageKey  = j.value("largeImageKey",  "");
    c.largeImageText = j.value("largeImageText", "");
    c.smallImageKey  = j.value("smallImageKey",  "");
    c.smallImageText = j.value("smallImageText", "");
    c.musicMode      = j.value("musicMode",      false);
    c.useJoinButtons = j.value("useJoinButtons", false);

    if (j.contains("buttons") && j["buttons"].is_array()) {
        int idx = 0;
        for (auto& b : j["buttons"]) {
            if (idx >= 2) break;
            c.buttons[idx].label = b.value("label", "");
            c.buttons[idx].url   = b.value("url",   "");
            ++idx;
        }
    }
    return c;
}

// ── Defaults ────────────────────────────────────────────────
std::vector<Preset> PresetManager::CreateDefaults() {
    return {
        {u8"Study",  u8"\U0001F4DA", {"","Studying hard",    "Do not disturb","","",   "","",{},false,false}, {0.18f,0.80f,0.44f}},
        {u8"Work",   u8"\U0001F4BB", {"","Working",          "In the zone",   "","",   "","",{},false,false}, {0.20f,0.60f,0.86f}},
        {u8"Gaming", u8"\U0001F3AE", {"","Gaming",           "Having fun",    "","",   "","",{},false,false}, {0.90f,0.30f,0.30f}},
        {u8"Coding", u8"\u2615",     {"","Writing code",     "In the flow",   "","",   "","",{},false,false}, {0.93f,0.74f,0.20f}},
    };
}

// ── Load ─────────────────────────────────────────────────────
std::vector<Preset> PresetManager::LoadPresets(const std::string& path) {
    if (!fs::exists(path)) return CreateDefaults();

    try {
        std::ifstream f(path);
        json arr = json::parse(f);
        std::vector<Preset> out;
        for (auto& item : arr) {
            Preset p;
            p.name  = item.value("name",  "Unnamed");
            p.emoji = item.value("emoji", "");
            if (item.contains("config"))
                p.config = JsonToConfig(item["config"]);
            if (item.contains("color") && item["color"].is_array()
                && item["color"].size() == 3) {
                p.color.r = item["color"][0].get<float>();
                p.color.g = item["color"][1].get<float>();
                p.color.b = item["color"][2].get<float>();
            }
            out.push_back(std::move(p));
        }
        return out.empty() ? CreateDefaults() : out;
    } catch (...) {
        return CreateDefaults();
    }
}

// ── Save ─────────────────────────────────────────────────────
void PresetManager::SavePresets(const std::string& path,
                                 const std::vector<Preset>& presets) {
    json arr = json::array();
    for (auto& p : presets) {
        json item;
        item["name"]   = p.name;
        item["emoji"]  = p.emoji;
        item["config"] = ConfigToJson(p.config);
        item["color"]  = {p.color.r, p.color.g, p.color.b};
        arr.push_back(item);
    }

    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path);
    f << arr.dump(2);
}

// ── Apply ────────────────────────────────────────────────────
void PresetManager::ApplyPreset(const Preset& preset, AppState& state) {
    std::lock_guard<std::mutex> lk(state.mtx);
    // Preserve clientId from current config
    std::string savedId = state.rpcConfig.clientId;
    state.rpcConfig          = preset.config;
    state.rpcConfig.clientId = savedId;
    state.configDirty.store(true);
}
