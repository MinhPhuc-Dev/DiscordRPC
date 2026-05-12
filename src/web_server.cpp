#include "web_server.h"
#include "httplib.h"
#include <nlohmann/json.hpp>
#include <thread>
#include <fstream>
#include <sstream>
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

static httplib::Server* g_server = nullptr;
static std::thread       g_serverThread;

// ── WinHTTP helper: simple GET returning body string ────────
static std::string WinHttpGet(const std::wstring& host, const std::wstring& path, bool https = true) {
    HINTERNET hSession = WinHttpOpen(L"DiscordRPCManager/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(),
        https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    DWORD flags = WINHTTP_FLAG_REFRESH | (https ? WINHTTP_FLAG_SECURE : 0);
    HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return ""; }

    WinHttpAddRequestHeaders(hReq, L"Accept: application/json", -1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hReq, L"User-Agent: Mozilla/5.0", -1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return "";
    }

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        std::string chunk(avail, '\0');
        DWORD read = 0;
        WinHttpReadData(hReq, &chunk[0], avail, &read);
        chunk.resize(read);
        body += chunk;
    }

    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return body;
}

static std::string UrlEncodeA(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c=='-'||c=='_'||c=='.'||c=='~') out += c;
        else { char buf[4]; sprintf_s(buf, "%%%02X", c); out += buf; }
    }
    return out;
}

static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// ── Read bundled HTML from assets/web_ui.html ───────────────
static std::string ReadWebUI() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string dir(buf);
    auto pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos);
    std::string path = dir + "\\assets\\web_ui.html";
    std::ifstream f(path, std::ios::binary);
    if (!f) return "<h1>web_ui.html not found in assets/</h1>";
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// ── Build JSON snapshot of current state ─────────────────────
static json StateToJson(AppState& state) {
    std::lock_guard<std::mutex> lk(state.mtx);
    json j;
    j["connected"]      = state.rpcConnected;
    j["username"]       = state.connectedUsername;
    j["clientId"]       = state.rpcConfig.clientId;
    j["details"]        = state.rpcConfig.details;
    j["state"]          = state.rpcConfig.state;
    j["largeImageKey"]  = state.rpcConfig.largeImageKey;
    j["largeImageText"] = state.rpcConfig.largeImageText;
    j["smallImageKey"]  = state.rpcConfig.smallImageKey;
    j["smallImageText"] = state.rpcConfig.smallImageText;
    j["musicMode"]      = state.rpcConfig.musicMode;
    j["useJoinButtons"] = state.rpcConfig.useJoinButtons;
    j["button1Label"]   = state.rpcConfig.buttons[0].label;
    j["button1Url"]     = state.rpcConfig.buttons[0].url;
    j["button2Label"]   = state.rpcConfig.buttons[1].label;
    j["button2Url"]     = state.rpcConfig.buttons[1].url;
    j["statusMessage"]  = state.statusMessage;
    j["mediaTitle"]     = state.media.title;
    j["mediaArtist"]    = state.media.artist;
    j["mediaPlaying"]   = state.media.isPlaying;

    json presets = json::array();
    for (int i = 0; i < (int)state.presets.size(); ++i) {
        auto& p = state.presets[i];
        json pj;
        pj["index"]   = i;
        pj["name"]    = p.name;
        pj["emoji"]   = p.emoji;
        pj["active"]  = (i == state.activePresetIndex);
        pj["color"]   = {p.color.r, p.color.g, p.color.b};
        pj["details"] = p.config.details;
        pj["state"]   = p.config.state;
        pj["largeImageKey"]   = p.config.largeImageKey;
        pj["largeImageText"]  = p.config.largeImageText;
        pj["smallImageKey"]   = p.config.smallImageKey;
        pj["smallImageText"]  = p.config.smallImageText;
        pj["button1Label"]    = p.config.buttons[0].label;
        pj["button1Url"]      = p.config.buttons[0].url;
        pj["button2Label"]    = p.config.buttons[1].label;
        pj["button2Url"]      = p.config.buttons[1].url;
        pj["musicMode"]       = p.config.musicMode;
        pj["useJoinButtons"]  = p.config.useJoinButtons;
        presets.push_back(pj);
    }
    j["presets"] = presets;
    return j;
}

// ── Start ─────────────────────────────────────────────────────
void WebServer::Start(AppState& state, int port) {
    if (g_server) return;
    g_server = new httplib::Server();

    // Serve main UI
    g_server->Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(ReadWebUI(), "text/html; charset=utf-8");
    });

    // State API
    g_server->Get("/api/state", [&state](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(StateToJson(state).dump(), "application/json");
    });

    // Apply preset
    g_server->Post("/api/preset/apply", [&state](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto body = json::parse(req.body);
            int idx = body.value("index", -1);
            std::lock_guard<std::mutex> lk(state.mtx);
            if (idx >= 0 && idx < (int)state.presets.size()) {
                auto& p = state.presets[idx];
                state.rpcConfig = p.config;
                state.activePresetIndex = idx;
                state.configDirty.store(true);
            }
            res.set_content("{\"ok\":true}", "application/json");
        } catch (...) { res.set_content("{\"ok\":false}", "application/json"); }
    });

    // Save current config to a preset slot
    g_server->Post("/api/preset/save", [&state](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto body = json::parse(req.body);
            int idx = body.value("index", -1);
            std::lock_guard<std::mutex> lk(state.mtx);
            if (idx >= 0 && idx < (int)state.presets.size()) {
                auto& p = state.presets[idx];
                if (body.contains("name"))          p.name                     = body["name"];
                if (body.contains("emoji"))         p.emoji                    = body["emoji"];
                if (body.contains("details"))       p.config.details           = body["details"];
                if (body.contains("state"))         p.config.state             = body["state"];
                if (body.contains("largeImageKey")) p.config.largeImageKey     = body["largeImageKey"];
                if (body.contains("largeImageText"))p.config.largeImageText    = body["largeImageText"];
                if (body.contains("smallImageKey")) p.config.smallImageKey     = body["smallImageKey"];
                if (body.contains("smallImageText"))p.config.smallImageText    = body["smallImageText"];
                if (body.contains("button1Label"))  p.config.buttons[0].label  = body["button1Label"];
                if (body.contains("button1Url"))    p.config.buttons[0].url    = body["button1Url"];
                if (body.contains("button2Label"))  p.config.buttons[1].label  = body["button2Label"];
                if (body.contains("button2Url"))    p.config.buttons[1].url    = body["button2Url"];
                if (body.contains("musicMode"))     p.config.musicMode         = body["musicMode"];
                if (body.contains("useJoinButtons"))p.config.useJoinButtons    = body["useJoinButtons"];
                if (body.contains("colorR"))        p.color.r = body["colorR"];
                if (body.contains("colorG"))        p.color.g = body["colorG"];
                if (body.contains("colorB"))        p.color.b = body["colorB"];
                state.rpcConfig = p.config;
                state.activePresetIndex = idx;
                state.configDirty.store(true);
            }
            res.set_content("{\"ok\":true}", "application/json");
        } catch (...) { res.set_content("{\"ok\":false}", "application/json"); }
    });

    // Live update (no preset save)
    g_server->Post("/api/update", [&state](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto body = json::parse(req.body);
            std::lock_guard<std::mutex> lk(state.mtx);
            if (body.contains("details"))       state.rpcConfig.details           = body["details"];
            if (body.contains("state"))         state.rpcConfig.state             = body["state"];
            if (body.contains("largeImageKey")) state.rpcConfig.largeImageKey     = body["largeImageKey"];
            if (body.contains("largeImageText"))state.rpcConfig.largeImageText    = body["largeImageText"];
            if (body.contains("smallImageKey")) state.rpcConfig.smallImageKey     = body["smallImageKey"];
            if (body.contains("smallImageText"))state.rpcConfig.smallImageText    = body["smallImageText"];
            if (body.contains("button1Label"))  state.rpcConfig.buttons[0].label  = body["button1Label"];
            if (body.contains("button1Url"))    state.rpcConfig.buttons[0].url    = body["button1Url"];
            if (body.contains("button2Label"))  state.rpcConfig.buttons[1].label  = body["button2Label"];
            if (body.contains("button2Url"))    state.rpcConfig.buttons[1].url    = body["button2Url"];
            if (body.contains("musicMode"))     state.rpcConfig.musicMode         = body["musicMode"];
            if (body.contains("useJoinButtons"))state.rpcConfig.useJoinButtons    = body["useJoinButtons"];
            state.configDirty.store(true);
            res.set_content("{\"ok\":true}", "application/json");
        } catch (...) { res.set_content("{\"ok\":false}", "application/json"); }
    });

    // Connect / disconnect
    g_server->Post("/api/connect", [&state](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        try {
            auto body = json::parse(req.body);
            std::string cid = body.value("clientId", "");
            std::lock_guard<std::mutex> lk(state.mtx);
            state.rpcConfig.clientId = cid;
            state.needReconnect.store(true);
            res.set_content("{\"ok\":true}", "application/json");
        } catch (...) { res.set_content("{\"ok\":false}", "application/json"); }
    });

    g_server->Post("/api/disconnect", [&state](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        state.needDisconnect.store(true);
        res.set_content("{\"ok\":true}", "application/json");
    });

    // ── Search: Pinterest images via DuckDuckGo ──────────────
    g_server->Get("/api/search/image", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        std::string q = req.get_param_value("q");
        if (q.empty()) { res.set_content("[]", "application/json"); return; }

        // Step 1: get vqd token from DuckDuckGo
        std::string ddgQ = UrlEncodeA(q + " site:pinterest.com");
        std::string html = WinHttpGet(L"duckduckgo.com",
            ToWide("/?q=" + ddgQ + "&ia=images"), true);

        // Extract vqd token using simple search
        std::string vqd;
        const std::string vqdKey = "vqd=\"";
        auto vp = html.find(vqdKey);
        if (vp == std::string::npos) { const std::string k2="vqd='"; vp=html.find(k2); if(vp!=std::string::npos){vp+=k2.size(); auto e=html.find('\'',vp); if(e!=std::string::npos)vqd=html.substr(vp,e-vp);}}
        else { vp+=vqdKey.size(); auto e=html.find('"',vp); if(e!=std::string::npos)vqd=html.substr(vp,e-vp); }

        json results = json::array();
        if (!vqd.empty()) {
            // Step 2: fetch image results JSON
            std::string imgPath = "/i.js?l=us-en&o=json&q=" + ddgQ + "&vqd=" + UrlEncodeA(vqd) + "&f=,,,,,&p=1";
            std::string imgJson = WinHttpGet(L"duckduckgo.com", ToWide(imgPath), true);
            try {
                auto root = json::parse(imgJson);
                for (auto& item : root["results"]) {
                    json r;
                    r["url"]   = item.value("image", "");
                    r["thumb"] = item.value("thumbnail", "");
                    r["label"] = item.value("title", q);
                    if (!r["url"].get<std::string>().empty())
                        results.push_back(r);
                    if ((int)results.size() >= 12) break;
                }
            } catch (...) {}
        }
        // Fallback: generate unsplash links if Pinterest returned nothing
        if (results.empty()) {
            for (int i=1;i<=12;++i) {
                json r;
                r["url"] = "https://source.unsplash.com/300x300/?" + UrlEncodeA(q) + "&sig=" + std::to_string(i);
                r["thumb"] = r["url"];
                r["label"] = q + " #" + std::to_string(i);
                results.push_back(r);
            }
        }
        res.set_content(results.dump(), "application/json");
    });

    // ── Search: music via Deezer (free, no auth) ─────────────
    g_server->Get("/api/search/music", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        std::string q = req.get_param_value("q");
        if (q.empty()) { res.set_content("[]", "application/json"); return; }

        std::string enc = UrlEncodeA(q);
        std::string body = WinHttpGet(L"api.deezer.com",
            ToWide("/search?q=" + enc + "&limit=12"), true);

        json results = json::array();
        try {
            auto root = json::parse(body);
            for (auto& item : root["data"]) {
                json r;
                r["title"]     = item.value("title", "");
                r["artist"]    = item["artist"].value("name", "");
                r["album"]     = item["album"].value("title", "");
                // album art (xl = 1000x1000)
                r["thumb"]     = item["album"].value("cover_xl", item["album"].value("cover_big", ""));
                r["deezerUrl"] = item.value("link", "");
                // Generate YouTube Music and Spotify search URLs from artist+title
                std::string ytQ  = UrlEncodeA(r["artist"].get<std::string>() + " " + r["title"].get<std::string>());
                r["ytMusicUrl"]  = "https://music.youtube.com/search?q=" + ytQ;
                r["spotifyUrl"]  = "https://open.spotify.com/search/" + ytQ;
                results.push_back(r);
            }
        } catch (...) {}
        res.set_content(results.dump(), "application/json");
    });

    // ── Search: GIFs via Tenor (no key) ──────────────────────
    g_server->Get("/api/search/gif", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        std::string q = req.get_param_value("q");
        if (q.empty()) { res.set_content("[]", "application/json"); return; }

        std::string enc = UrlEncodeA(q);
        // Tenor v2 public demo key
        std::string path = "/v2/search?q=" + enc + "&key=LIVDSRZULELA&limit=12&media_filter=gif";
        std::string body = WinHttpGet(L"tenor.googleapis.com", ToWide(path), true);

        json results = json::array();
        try {
            auto root = json::parse(body);
            for (auto& item : root["results"]) {
                json r;
                r["title"] = item.value("title", q);
                auto& med = item["media_formats"]["gif"];
                r["url"]   = med["url"];
                r["thumb"] = item["media_formats"]["tinygif"]["url"];
                results.push_back(r);
            }
        } catch (...) {}
        res.set_content(results.dump(), "application/json");
    });

    g_serverThread = std::thread([port]() {
        g_server->listen("127.0.0.1", port);
    });
}

void WebServer::Stop() {
    if (g_server) {
        g_server->stop();
        if (g_serverThread.joinable()) g_serverThread.join();
        delete g_server;
        g_server = nullptr;
    }
}
