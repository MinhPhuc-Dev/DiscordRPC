#pragma once
#include "app_state.h"

class WebServer {
public:
    static void Start(AppState& state, int port = 8080);
    static void Stop();
};
