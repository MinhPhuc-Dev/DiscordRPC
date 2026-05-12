#include "media_watcher.h"
#include "utils.h"
#include <thread>
#include <chrono>

// ── C++/WinRT headers for SMTC ──────────────────────────────
#pragma comment(lib, "windowsapp")
#pragma comment(lib, "runtimeobject")

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

using namespace winrt;
using namespace Windows::Media::Control;

// ── Init ─────────────────────────────────────────────────────
bool MediaWatcher::Initialize() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        m_initialized = true;
        return true;
    } catch (...) {
        m_initialized = false;
        return false;
    }
}

// ── Poll current session ─────────────────────────────────────
void MediaWatcher::Poll(AppState& state) {
    if (!m_initialized) return;

    try {
        auto manager =
            GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        auto session = manager.GetCurrentSession();

        if (!session) {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.media.isPlaying = false;
            return;
        }

        // ── Media properties ──
        auto props = session.TryGetMediaPropertiesAsync().get();
        std::string title  = Utils::WStringToString(std::wstring(props.Title()));
        std::string artist = Utils::WStringToString(std::wstring(props.Artist()));
        std::string album  = Utils::WStringToString(std::wstring(props.AlbumTitle()));

        // ── Playback info ──
        auto pbInfo  = session.GetPlaybackInfo();
        bool playing = (pbInfo.PlaybackStatus() ==
            GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

        // ── Timeline ──
        auto timeline = session.GetTimelineProperties();
        double position = std::chrono::duration<double>(
            timeline.Position()).count();
        double duration = std::chrono::duration<double>(
            timeline.EndTime() - timeline.StartTime()).count();

        // ── Source app ──
        std::string source = Utils::WStringToString(
            std::wstring(session.SourceAppUserModelId()));
        // Simplify known sources
        if (source.find("Spotify") != std::string::npos)
            source = "Spotify";
        else if (source.find("Google") != std::string::npos ||
                 source.find("YouTube") != std::string::npos)
            source = "YouTube Music";

        // ── Update shared state ──
        std::string trackKey = artist + "|" + title;

        {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.media.title           = title;
            state.media.artist          = artist;
            state.media.album           = album;
            state.media.durationSeconds = duration;
            state.media.positionSeconds = position;
            state.media.isPlaying       = playing;
            state.media.sourceApp       = source;
        }

        // ── Detect song change ──
        if (trackKey != m_lastTrackKey && !title.empty()) {
            m_lastTrackKey = trackKey;
            state.songChanged.store(true);
            state.configDirty.store(true);
        }

        // Position changes → mark dirty in music mode
        {
            std::lock_guard<std::mutex> lk(state.mtx);
            if (state.rpcConfig.musicMode && playing)
                state.configDirty.store(true);
        }

    } catch (...) {
        // SMTC not available or session lost – silently ignore
    }
}

// ── Thread loop ──────────────────────────────────────────────
void MediaWatcher::RunLoop(AppState& state) {
    Initialize();

    while (!state.shouldQuit.load()) {
        Poll(state);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}
