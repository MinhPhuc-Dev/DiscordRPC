#include "discord_manager.h"
#include "utils.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>

using json = nlohmann::json;

enum IPC_Opcode { OP_HANDSHAKE = 0, OP_FRAME = 1, OP_CLOSE = 2 };

// ── Lifecycle ────────────────────────────────────────────────
DiscordManager::~DiscordManager() { Disconnect(); }

bool DiscordManager::Connect(const std::string& clientId) {
    Disconnect();
    m_clientId = clientId;
    if (!OpenPipe()) return false;
    if (!Handshake(clientId)) { ClosePipe(); return false; }
    m_connected = true;
    m_connectionTimeEpoch = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return true;
}

void DiscordManager::Disconnect() {
    if (m_connected) {
        // Clear activity before closing
        json cmd;
        cmd["cmd"]  = "SET_ACTIVITY";
        cmd["args"]["pid"] = static_cast<int>(GetCurrentProcessId());
        cmd["nonce"]       = Utils::GenerateNonce();
        SendOpcode(OP_FRAME, cmd.dump());

        SendOpcode(OP_CLOSE, "{}");
    }
    ClosePipe();
    m_connected = false;
    m_username.clear();
}

// ── Named Pipe I/O ──────────────────────────────────────────
bool DiscordManager::OpenPipe() {
    for (int i = 0; i < 10; ++i) {
        std::string name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        m_pipe = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                             0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (m_pipe != INVALID_HANDLE_VALUE) return true;
    }
    return false;
}

void DiscordManager::ClosePipe() {
    if (m_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(m_pipe);
        m_pipe = INVALID_HANDLE_VALUE;
    }
}

bool DiscordManager::SendOpcode(int opcode, const std::string& payload) {
    if (m_pipe == INVALID_HANDLE_VALUE) return false;
    uint32_t op  = static_cast<uint32_t>(opcode);
    uint32_t len = static_cast<uint32_t>(payload.size());
    DWORD w;
    if (!WriteFile(m_pipe, &op,  4, &w, nullptr) || w != 4) return false;
    if (!WriteFile(m_pipe, &len, 4, &w, nullptr) || w != 4) return false;
    if (len > 0) {
        if (!WriteFile(m_pipe, payload.data(), len, &w, nullptr) || w != len)
            return false;
    }
    return true;
}

bool DiscordManager::ReadResponse(int& opcode, std::string& payload) {
    if (m_pipe == INVALID_HANDLE_VALUE) return false;
    uint32_t op, len;
    DWORD r;
    if (!ReadFile(m_pipe, &op,  4, &r, nullptr) || r != 4) return false;
    if (!ReadFile(m_pipe, &len, 4, &r, nullptr) || r != 4) return false;
    opcode = static_cast<int>(op);
    if (len > 0 && len < 65536) {
        payload.resize(len);
        DWORD total = 0;
        while (total < len) {
            if (!ReadFile(m_pipe, &payload[total], len - total, &r, nullptr))
                return false;
            total += r;
        }
    }
    return true;
}

bool DiscordManager::Handshake(const std::string& clientId) {
    json hs; hs["v"] = 1; hs["client_id"] = clientId;
    if (!SendOpcode(OP_HANDSHAKE, hs.dump())) return false;

    int op; std::string resp;
    if (!ReadResponse(op, resp) || op != OP_FRAME) return false;

    try {
        auto j = json::parse(resp);
        if (j.contains("data") && j["data"].contains("user"))
            m_username = j["data"]["user"].value("username", "Unknown");
    } catch (...) {}
    return true;
}

// ── Lyrics helper ───────────────────────────────────────────
static int GetCurrentLyricIndex(const LyricData& lyrics, double pos) {
    if (lyrics.lines.empty() || !lyrics.isSynced) return -1;
    int currentIdx = -1;
    // Add small tolerance (e.g., 200ms) to avoid jitter
    for (int i = 0; i < (int)lyrics.lines.size(); ++i) {
        if (lyrics.lines[i].timestamp <= pos + 0.2) {
            currentIdx = i;
        } else {
            break;
        }
    }
    return currentIdx;
}

std::string DiscordManager::GetCurrentLyricLine(
    const LyricData& lyrics, double pos)
{
    int idx = GetCurrentLyricIndex(lyrics, pos);
    return idx >= 0 ? lyrics.lines[idx].text : "";
}

std::string DiscordManager::GetNextLyricLine(
    const LyricData& lyrics, double pos)
{
    int idx = GetCurrentLyricIndex(lyrics, pos);
    if (idx >= 0 && idx + 1 < (int)lyrics.lines.size()) {
        return lyrics.lines[idx + 1].text;
    }
    return u8"\u266A"; // End of song / no next line
}



// ── Build SET_ACTIVITY payload ──────────────────────────────
std::string DiscordManager::BuildActivityJSON(AppState& state) {
    std::lock_guard<std::mutex> lk(state.mtx);
    json activity;

    if (state.rpcConfig.musicMode && state.media.isPlaying) {
        // ── Music mode ──
        std::string lyric = GetCurrentLyricLine(state.lyrics,
                                                 state.media.positionSeconds);
        if (lyric.empty()) lyric = u8"\u266A \u266B \u266A";

        std::string nextLyric = GetNextLyricLine(state.lyrics,
                                                 state.media.positionSeconds);
        if (nextLyric.empty()) nextLyric = u8"\u266A " + state.media.artist + " - " + state.media.title;

        activity["details"] = Utils::TruncateUTF8(lyric, 128);
        activity["state"]   = Utils::TruncateUTF8(nextLyric, 128);

        if (state.media.durationSeconds > 0) {
            auto now = std::chrono::system_clock::now();
            auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                             now.time_since_epoch()).count();
            activity["timestamps"]["start"] =
                epoch - static_cast<int64_t>(state.media.positionSeconds);
            activity["timestamps"]["end"] =
                epoch + static_cast<int64_t>(
                    state.media.durationSeconds - state.media.positionSeconds);
        }
        if (!state.rpcConfig.largeImageKey.empty()) {
            activity["assets"]["large_image"] = state.rpcConfig.largeImageKey;
            activity["assets"]["large_text"]  =
                state.media.album.empty() ? state.media.title
                                          : state.media.album;
        }
        if (!state.rpcConfig.smallImageKey.empty()) {
            activity["assets"]["small_image"] = state.rpcConfig.smallImageKey;
            activity["assets"]["small_text"]  = state.media.sourceApp;
        }
    } else {
        // ── Normal mode ──
        if (!state.rpcConfig.details.empty())
            activity["details"] = Utils::TruncateUTF8(state.rpcConfig.details, 128);
        if (!state.rpcConfig.state.empty())
            activity["state"] = Utils::TruncateUTF8(state.rpcConfig.state, 128);

        if (!state.rpcConfig.largeImageKey.empty()) {
            activity["assets"]["large_image"] = state.rpcConfig.largeImageKey;
            if (!state.rpcConfig.largeImageText.empty())
                activity["assets"]["large_text"] = state.rpcConfig.largeImageText;
        }
        if (!state.rpcConfig.smallImageKey.empty()) {
            activity["assets"]["small_image"] = state.rpcConfig.smallImageKey;
            if (!state.rpcConfig.smallImageText.empty())
                activity["assets"]["small_text"] = state.rpcConfig.smallImageText;
        }
        
        // Show elapsed time in Normal Mode
        if (m_connectionTimeEpoch > 0) {
            activity["timestamps"]["start"] = m_connectionTimeEpoch;
        }
    }

    // Buttons (max 2) vs Join Request
    if (state.rpcConfig.useJoinButtons) {
        // Discord API drops custom buttons if 'secrets' or 'party' is included
        activity["party"]["id"] = "party_" + state.rpcConfig.clientId;
        activity["party"]["size"] = {1, 2}; // Required to be > 0 for Join button
        activity["secrets"]["join"] = "join_" + state.rpcConfig.clientId;
    } else {
        json btns = json::array();
        for (int i = 0; i < 2; ++i) {
            auto& b = state.rpcConfig.buttons[i];
            if (!b.label.empty() && !b.url.empty())
                btns.push_back({{"label", Utils::TruncateUTF8(b.label, 32)},
                                {"url",   b.url}});
        }
        if (!btns.empty()) {
            activity["buttons"] = btns;
        }
    }

    activity["instance"] = true;

    json cmd;
    cmd["cmd"]  = "SET_ACTIVITY";
    cmd["args"]["pid"]      = static_cast<int>(GetCurrentProcessId());
    cmd["args"]["activity"] = activity;
    cmd["nonce"]            = Utils::GenerateNonce();
    return cmd.dump();
}

// ── Update Presence ─────────────────────────────────────────
void DiscordManager::UpdatePresence(AppState& state) {
    if (!m_connected) return;
    std::string payload = BuildActivityJSON(state);
    state.Log("Updating presence...");
    if (!SendOpcode(OP_FRAME, payload)) {
        m_connected = false;
        state.needReconnect.store(true);
        {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.rpcConnected = false;
            state.lastError = "Lost connection to Discord";
        }
        state.Log("Error: Lost connection to Discord");
        return;
    }
    // Drain any pending response
    DWORD avail = 0;
    if (PeekNamedPipe(m_pipe, nullptr, 0, nullptr, &avail, nullptr)
        && avail > 0) {
        int op; std::string resp;
        ReadResponse(op, resp);
    }
}

// ── Thread loop ─────────────────────────────────────────────
void DiscordManager::RunLoop(AppState& state) {
    using clock = std::chrono::steady_clock;

    while (!state.shouldQuit.load()) {
        // Disconnect if requested
        if (state.needDisconnect.load()) {
            Disconnect();
            state.needDisconnect.store(false);
            std::lock_guard<std::mutex> lk(state.mtx);
            state.statusMessage = "Disconnected";
        }

        // Reconnect if needed
        if (state.needReconnect.load()) {
            std::string cid;
            { std::lock_guard<std::mutex> lk(state.mtx); cid = state.rpcConfig.clientId; }
            state.Log("Attempting connection to Discord with client ID: " + cid);
            if (!cid.empty()) {
                if (Connect(cid)) {
                    {
                        std::lock_guard<std::mutex> lk(state.mtx);
                        state.rpcConnected      = true;
                        state.connectedUsername  = m_username;
                        state.statusMessage     = "Connected as " + m_username;
                        state.lastError.clear();
                        state.configDirty.store(true);
                    }
                    state.Log("Successfully connected to Discord as " + m_username);
                    state.needReconnect.store(false);
                } else {
                    state.Log("Failed to connect to Discord, retrying...");
                    // sleep a bit longer before retrying to avoid spamming the log
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                }
            } else {
                state.needReconnect.store(false);
            }
        }

        // Check rate limit
        bool dirty = state.configDirty.load();
        bool musicTick = false;
        {
            std::lock_guard<std::mutex> lk(state.mtx);
            musicTick = state.rpcConfig.musicMode && state.media.isPlaying;
        }

        auto now = clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - state.lastRPCUpdate).count();

        if (m_connected && elapsed >= RATE_LIMIT_MS && (dirty || musicTick)) {
            UpdatePresence(state);
            state.configDirty.store(false);
            state.lastRPCUpdate = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    Disconnect();
}
