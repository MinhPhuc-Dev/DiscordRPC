/*
 *  Discord Rich Presence Manager
 *  Entry point – spawns worker threads, runs UI on main thread.
 */

#include "app_state.h"
#include "discord_manager.h"
#include "media_watcher.h"
#include "lyric_fetcher.h"
#include "preset_manager.h"
#include "ui_manager.h"
#include "utils.h"
#include "web_server.h"

#include <thread>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // ── Shared state ──
    AppState state;

    // ── Load presets ──
    std::string presetsDir  = Utils::GetExeDirectory() + "\\presets";
    std::string presetsPath = presetsDir + "\\user_presets.json";
    std::string defaultsPath = Utils::GetExeDirectory() + "\\assets\\presets_default.json";

    state.presets = PresetManager::LoadPresets(presetsPath);
    if (state.presets.empty())
        state.presets = PresetManager::LoadPresets(defaultsPath);
    if (state.presets.empty())
        state.presets = PresetManager::CreateDefaults();

    // ── Initialize UI ──
    UIManager ui;
    if (!ui.Initialize(600, 700, "Discord RPC Manager")) {
        MessageBoxA(nullptr, "Failed to initialize UI (GLFW/OpenGL)",
                    "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // ── Spawn worker threads ──
    DiscordManager discord;
    MediaWatcher   mediaWatcher;
    LyricFetcher   lyricFetcher;

    std::thread discordThread([&]() { discord.RunLoop(state); });
    std::thread mediaThread([&]()   { mediaWatcher.RunLoop(state); });
    std::thread lyricsThread([&]()  { lyricFetcher.RunLoop(state); });

    // ── Start Web Server ──
    WebServer::Start(state, 1337);

    // ── Main loop (UI) ──
    while (!ui.ShouldClose() && !state.shouldQuit.load()) {
        if (!ui.BeginFrame()) break;
        ui.Render(state);
        ui.EndFrame();
    }

    // ── Graceful shutdown ──
    state.shouldQuit.store(true);

    if (discordThread.joinable()) discordThread.join();
    if (mediaThread.joinable())   mediaThread.join();
    if (lyricsThread.joinable())  lyricsThread.join();

    WebServer::Stop();

    // Save presets
    PresetManager::SavePresets(presetsPath, state.presets);

    ui.Shutdown();
    return 0;
}
