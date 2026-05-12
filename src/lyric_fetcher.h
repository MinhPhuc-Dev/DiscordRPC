#pragma once

#include "app_state.h"
#include <string>
#include <vector>

class LyricFetcher {
public:
    LyricFetcher()  = default;
    ~LyricFetcher() = default;

    // Fetch from lrclib.net and update state
    void FetchForCurrentSong(AppState& state);

    // Parse LRC format string → sorted LyricLine vector
    static std::vector<LyricLine> ParseLRC(const std::string& lrc);

    // Get lyric line for a given timestamp
    static std::string GetLineAtPosition(
        const std::vector<LyricLine>& lines, double pos);

    // Background watcher – triggers on songChanged and importUrl
    void RunLoop(AppState& state);

    // Import song from YouTube Music URL
    void ImportFromYouTubeUrl(const std::string& url, AppState& state);

private:
    // WinHTTP GET request
    std::string HttpGet(const std::string& host,
                        const std::string& path);
};
