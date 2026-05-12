#pragma once

#include "app_state.h"
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class DiscordManager {
public:
    DiscordManager()  = default;
    ~DiscordManager();

    bool Connect(const std::string& clientId);
    void Disconnect();
    bool IsConnected() const { return m_connected; }

    void UpdatePresence(AppState& state);
    void RunLoop(AppState& state);          // thread entry

    std::string GetUsername() const { return m_username; }

private:
    // IPC primitives
    bool OpenPipe();
    void ClosePipe();
    bool SendOpcode(int opcode, const std::string& payload);
    bool ReadResponse(int& opcode, std::string& payload);
    bool Handshake(const std::string& clientId);

    // Helpers
    std::string BuildActivityJSON(AppState& state);
    std::string GetCurrentLyricLine(const LyricData& lyrics, double position);
    std::string GetNextLyricLine(const LyricData& lyrics, double position);

    HANDLE      m_pipe      = INVALID_HANDLE_VALUE;
    bool        m_connected = false;
    std::string m_clientId;
    std::string m_username;
    int64_t     m_connectionTimeEpoch = 0;

    static constexpr int RATE_LIMIT_MS = 5000;
};
