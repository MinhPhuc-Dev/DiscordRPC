#include "lyric_fetcher.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <regex>
#include <thread>
#include <chrono>
#include <sstream>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp")

using json = nlohmann::json;

// ── WinHTTP GET (HTTPS) ─────────────────────────────────────
std::string LyricFetcher::HttpGet(const std::string& host,
                                   const std::string& path) {
    std::string result;
    std::wstring wHost = Utils::StringToWString(host);
    std::wstring wPath = Utils::StringToWString(path);

    HINTERNET hSession = WinHttpOpen(
        L"DiscordRPCManager/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(
        hSession, wHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect, L"GET", wPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS,
                           0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD size = 0;
        do {
            WinHttpQueryDataAvailable(hRequest, &size);
            if (size == 0) break;
            std::vector<char> buf(size);
            DWORD downloaded = 0;
            WinHttpReadData(hRequest, buf.data(), size, &downloaded);
            result.append(buf.data(), downloaded);
        } while (size > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

// ── Parse LRC format ────────────────────────────────────────
std::vector<LyricLine> LyricFetcher::ParseLRC(const std::string& lrc) {
    std::vector<LyricLine> lines;
    // Match [mm:ss.xx] or [mm:ss.xxx]
    std::regex re(R"(\[(\d+):(\d+)\.(\d+)\](.*))", std::regex::optimize);

    std::istringstream stream(lrc);
    std::string line;
    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, re)) {
            int minutes = std::stoi(match[1].str());
            int seconds = std::stoi(match[2].str());
            std::string fracStr = match[3].str();
            // Normalize to milliseconds
            double frac = std::stod("0." + fracStr);
            double timestamp = minutes * 60.0 + seconds + frac;
            std::string text = match[4].str();
            // Trim whitespace
            while (!text.empty() && (text.back() == '\r' || text.back() == '\n'
                   || text.back() == ' '))
                text.pop_back();
            if (!text.empty()) {
                lines.push_back({timestamp, text});
            }
        }
    }
    // Sort by timestamp
    std::sort(lines.begin(), lines.end(),
              [](const LyricLine& a, const LyricLine& b) {
                  return a.timestamp < b.timestamp;
              });
    return lines;
}

// ── Get line at position ────────────────────────────────────
std::string LyricFetcher::GetLineAtPosition(
    const std::vector<LyricLine>& lines, double pos)
{
    if (lines.empty()) return "";
    int lo = 0, hi = static_cast<int>(lines.size()) - 1, res = 0;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lines[mid].timestamp <= pos) { res = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return lines[res].text;
}

// ── Fetch lyrics from lrclib.net ────────────────────────────
void LyricFetcher::FetchForCurrentSong(AppState& state) {
    std::string title, artist, trackKey;
    double duration = 0;
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        title    = state.media.title;
        artist   = state.media.artist;
        duration = state.media.durationSeconds;
        trackKey = artist + "|" + title;

        // Already have lyrics for this track
        if (state.lyrics.trackKey == trackKey) return;
        state.lyrics.fetching = true;
    }

    // Build query
    std::string path = "/api/search?track_name="
                     + Utils::UrlEncode(title)
                     + "&artist_name=" + Utils::UrlEncode(artist);

    std::string body = HttpGet("lrclib.net", path);

    std::vector<LyricLine> parsed;
    bool synced = false;

    if (!body.empty()) {
        try {
            auto arr = json::parse(body);
            if (arr.is_array() && !arr.empty()) {
                // Prefer synced lyrics, try each result
                for (auto& item : arr) {
                    if (item.contains("syncedLyrics")
                        && !item["syncedLyrics"].is_null()) {
                        std::string lrc = item["syncedLyrics"].get<std::string>();
                        parsed = ParseLRC(lrc);
                        if (!parsed.empty()) { synced = true; break; }
                    }
                }
                // Fallback: plain lyrics
                if (!synced && arr[0].contains("plainLyrics")
                    && !arr[0]["plainLyrics"].is_null()) {
                    std::string plain = arr[0]["plainLyrics"].get<std::string>();
                    // Put all text as a single line at t=0
                    parsed.push_back({0.0, plain.substr(0, 128)});
                }
            }
        } catch (...) {}
    }

    // Update state
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        state.lyrics.lines    = std::move(parsed);
        state.lyrics.trackKey = trackKey;
        state.lyrics.isSynced = synced;
        state.lyrics.fetching = false;
    }
    state.configDirty.store(true);
}

// ── Import from YouTube Music URL ──────────────────────────
void LyricFetcher::ImportFromYouTubeUrl(const std::string& url,
                                         AppState& state) {
    // Extract video ID from various YouTube URL formats
    std::string videoId;
    std::regex ytRe(R"((?:v=|youtu\.be/)([a-zA-Z0-9_-]{11}))",
                    std::regex::optimize);
    std::smatch match;
    if (std::regex_search(url, match, ytRe)) {
        videoId = match[1].str();
    } else {
        std::lock_guard<std::mutex> lk(state.mtx);
        state.lastError = "Invalid YouTube URL";
        return;
    }

    // Use YouTube oEmbed to get metadata
    std::string oembedUrl = "/oembed?url=https://www.youtube.com/watch?v="
                          + videoId + "&format=json";
    std::string body = HttpGet("www.youtube.com", oembedUrl);

    std::string title, artist;
    if (!body.empty()) {
        try {
            auto j = json::parse(body);
            std::string fullTitle = j.value("title", "");
            std::string authorName = j.value("author_name", "");

            // YouTube Music titles are often "Artist - Song"
            auto dashPos = fullTitle.find(" - ");
            if (dashPos != std::string::npos) {
                artist = fullTitle.substr(0, dashPos);
                title  = fullTitle.substr(dashPos + 3);
            } else {
                title  = fullTitle;
                // Clean up author: remove " - Topic" suffix
                auto topicPos = authorName.find(" - Topic");
                artist = (topicPos != std::string::npos)
                    ? authorName.substr(0, topicPos)
                    : authorName;
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.lastError = "Failed to parse YouTube metadata";
            return;
        }
    } else {
        std::lock_guard<std::mutex> lk(state.mtx);
        state.lastError = "Failed to fetch YouTube metadata";
        return;
    }

    // Update media info
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        state.media.title     = title;
        state.media.artist    = artist;
        state.media.sourceApp = "YouTube Music (imported)";
        state.media.isPlaying = true;
        state.statusMessage   = "Imported: " + artist + " - " + title;
        state.lastError.clear();
    }

    // Trigger lyric fetch for imported song
    state.songChanged.store(true);
    state.configDirty.store(true);
}

// ── Background loop ─────────────────────────────────────────
void LyricFetcher::RunLoop(AppState& state) {
    while (!state.shouldQuit.load()) {
        // Handle YouTube URL import
        if (state.importUrl.load()) {
            state.importUrl.store(false);
            std::string url;
            { std::lock_guard<std::mutex> lk(state.mtx); url = state.youtubeImportUrl; }
            if (!url.empty()) {
                ImportFromYouTubeUrl(url, state);
            }
        }

        // Handle song change
        if (state.songChanged.load()) {
            state.songChanged.store(false);
            FetchForCurrentSong(state);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
