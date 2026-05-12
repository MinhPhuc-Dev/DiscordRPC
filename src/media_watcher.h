#pragma once

#include "app_state.h"
#include <string>

class MediaWatcher {
public:
    MediaWatcher()  = default;
    ~MediaWatcher() = default;

    bool Initialize();
    void Poll(AppState& state);
    void RunLoop(AppState& state);   // thread entry

private:
    std::string m_lastTrackKey;      // detect song changes
    bool        m_initialized = false;
};
