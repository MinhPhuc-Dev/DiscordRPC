#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <deque>


// ── Media Info (from SMTC) ───────────────────────────────────
struct MediaInfo {
    std::string title;
    std::string artist;
    std::string album;
    double      durationSeconds = 0.0;
    double      positionSeconds = 0.0;
    bool        isPlaying       = false;
    std::string sourceApp;       // "Spotify", "YouTube Music", etc.
};

// ── Synced Lyrics ────────────────────────────────────────────
struct LyricLine {
    double      timestamp;       // seconds from start
    std::string text;
};

struct LyricData {
    std::vector<LyricLine> lines;
    std::string trackKey;        // "artist|title" – avoid re-fetch
    bool        isSynced  = false;
    bool        fetching  = false;
};

// ── RPC Configuration ────────────────────────────────────────
struct RPCButton {
    std::string label;           // max 32 chars
    std::string url;
};

struct RPCConfig {
    std::string clientId;
    std::string details;
    std::string state;
    std::string largeImageKey;   // asset key OR full https:// URL
    std::string largeImageText;
    std::string smallImageKey;   // asset key OR full https:// URL
    std::string smallImageText;
    RPCButton   buttons[2];
    bool        musicMode = false;
    bool        useJoinButtons = false; // toggle between URL buttons and Ask to Join
};

// ── Preset Color ─────────────────────────────────────────────
struct PresetColor {
    float r = 0.345f, g = 0.396f, b = 0.949f; // default: blurple
};

// ── Preset Profile ───────────────────────────────────────────
struct Preset {
    std::string  name;
    std::string  emoji;
    RPCConfig    config;
    PresetColor  color;
};

// ── Shared Application State ─────────────────────────────────
struct AppState {
    mutable std::mutex mtx;

    // Media
    MediaInfo   media;
    LyricData   lyrics;

    // RPC
    RPCConfig   rpcConfig;
    bool        rpcConnected = false;
    std::string connectedUsername;

    // Presets
    std::vector<Preset> presets;
    int         activePresetIndex = -1;

    // Thread flags (lock-free)
    std::atomic<bool> shouldQuit   {false};
    std::atomic<bool> songChanged  {false};
    std::atomic<bool> configDirty  {false};
    std::atomic<bool> needReconnect{false};
    std::atomic<bool> needDisconnect{false};
    std::atomic<bool> importUrl    {false}; // trigger YouTube URL import

    // YouTube URL import
    std::string youtubeImportUrl;

    // Rate-limit tracking
    std::chrono::steady_clock::time_point lastRPCUpdate;

    // UI status
    std::string statusMessage;
    std::string lastError;

    // Advanced Logs
    std::deque<std::string> logMessages;

    void Log(const std::string& msg) {
        std::lock_guard<std::mutex> lk(mtx);
        logMessages.push_front(msg);
        if (logMessages.size() > 500) {
            logMessages.pop_back();
        }
    }
};
