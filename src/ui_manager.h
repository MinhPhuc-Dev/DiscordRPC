#pragma once

#include "app_state.h"
#include "preset_manager.h"
#include <string>
#include <chrono>

struct GLFWwindow;

class UIManager {
public:
    UIManager()  = default;
    ~UIManager() = default;

    bool Initialize(int width, int height, const char* title);
    void Shutdown();

    bool BeginFrame();
    void Render(AppState& state);
    void EndFrame();

    bool ShouldClose() const;

    // System Tray helpers
    void ShowTrayIcon();
    void HideTrayIcon();
    void RestoreWindow();

private:
    void SetupStyle();
    void RenderConnectionTab(AppState& state);
    void RenderPresenceTab(AppState& state);
    void RenderPresetsTab(AppState& state);
    void RenderMusicTab(AppState& state);
    void RenderAdvancedTab(AppState& state);
    void RenderPreviewTab(AppState& state);
    void RenderStatusBar(AppState& state);

    // File dialog helper
    std::string OpenImageFileDialog();

    GLFWwindow* m_window      = nullptr;
    std::string m_presetsPath;

    // Input buffers
    char m_clientId[128]       = {};
    char m_details[128]        = {};
    char m_state[128]          = {};
    char m_largeImgKey[256]    = {};   // now supports full URLs
    char m_largeImgText[128]   = {};
    char m_smallImgKey[256]    = {};   // now supports full URLs
    char m_smallImgText[128]   = {};
    char m_btn1Label[33]       = {};
    char m_btn1Url[512]        = {};
    char m_btn2Label[33]       = {};
    char m_btn2Url[512]        = {};
    char m_newPresetName[64]   = {};
    char m_youtubeUrl[512]     = {};

    // Preset color picker
    float m_newPresetColor[3]  = {0.345f, 0.396f, 0.949f};

    bool m_useJoinButtons = false;

    bool m_buffersSynced = false;

    // System Tray
    bool m_isMinimized = false;
    void* m_hwnd = nullptr; // native HWND
    void* m_originalWndProc = nullptr;

    // ── Animation state ──
    int   m_lastActivePreset   = -1;
    float m_presetSwitchAnim   = 0.0f;  // 0..1 fade for preset switch flash
    float m_lyricFadeAlpha     = 1.0f;
    std::string m_prevLyricLine;
    std::chrono::steady_clock::time_point m_lastLyricChange;

    // Preset card hover animations (per preset)
    float m_presetHoverAnim[32] = {};  // up to 32 presets
};
