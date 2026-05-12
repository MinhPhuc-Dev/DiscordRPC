#include "ui_manager.h"
#include "utils.h"
#include "lyric_fetcher.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#include <cstring>
#include <cmath>
#include <algorithm>

#include <commdlg.h>
#include <shellapi.h>
#pragma comment(lib, "comdlg32")
#pragma comment(lib, "shell32")

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define WM_TRAYICON (WM_USER + 1)

// ── Discord brand colors ────────────────────────────────────
static const ImVec4 kBlurple   = {0.345f, 0.396f, 0.949f, 1.0f};
static const ImVec4 kGreen     = {0.224f, 0.706f, 0.290f, 1.0f};
static const ImVec4 kRed       = {0.937f, 0.267f, 0.267f, 1.0f};
static const ImVec4 kDarkBg    = {0.110f, 0.118f, 0.133f, 1.0f};
static const ImVec4 kPanelBg   = {0.157f, 0.165f, 0.188f, 1.0f};
static const ImVec4 kInputBg   = {0.200f, 0.208f, 0.235f, 1.0f};
static const ImVec4 kTextDim   = {0.600f, 0.620f, 0.660f, 1.0f};
static const ImVec4 kAccentDim = {0.275f, 0.316f, 0.760f, 1.0f};

// Global instance for WndProc hook
static UIManager* s_instance = nullptr;

// ── Helper: copy string to fixed buffer ─────────────────────
static void SyncBuf(char* buf, size_t sz, const std::string& src) {
    std::string safe = Utils::TruncateUTF8(src, sz - 1);
    std::strncpy(buf, safe.c_str(), sz);
    buf[sz - 1] = '\0';
}

// ── Initialize ──────────────────────────────────────────────
bool UIManager::Initialize(int width, int height, const char* title) {
    if (!glfwInit()) return false;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_window) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // disable imgui.ini

    {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
        static const ImWchar extra[] = {
            0x2000, 0x2BFF, // Misc symbols, arrows, dingbats
            0x25A0, 0x25FF, // Geometric shapes (▶ ● ○ etc.)
            0
        };
        builder.AddRanges(extra);
        static ImVector<ImWchar> baseRanges;
        builder.BuildRanges(&baseRanges);
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 16.0f, &cfg, baseRanges.Data);
    }

    {
        ImFontConfig emoji_cfg;
        emoji_cfg.MergeMode   = true;
        emoji_cfg.OversampleH = 1;
        emoji_cfg.OversampleV = 1;
        static const ImWchar emoji_ranges[] = {
            0x1F004, 0x1FAFF, // Full emoji block (works with IMGUI_USE_WCHAR32)
            0
        };
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\seguiemj.ttf", 16.0f, &emoji_cfg, emoji_ranges);
    }

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    s_instance = this;

    SetupStyle();

    m_presetsPath = Utils::GetExeDirectory() + "\\presets\\user_presets.json";

    // ── System Tray Setup ──
    m_hwnd = glfwGetWin32Window(m_window);
    glfwSetWindowUserPointer(m_window, this);

    // Iconify callback (minimize)
    glfwSetWindowIconifyCallback(m_window, [](GLFWwindow* window, int iconified) {
        UIManager* ui = (UIManager*)glfwGetWindowUserPointer(window);
        if (iconified && ui) {
            ui->ShowTrayIcon();
        }
    });

    // Subclass WndProc to catch tray clicks
    m_originalWndProc = (void*)SetWindowLongPtr((HWND)m_hwnd, GWLP_WNDPROC, 
        (LONG_PTR)(WNDPROC)[](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
            if (msg == WM_TRAYICON && lp == WM_LBUTTONDBLCLK) {
                if (s_instance) s_instance->RestoreWindow();
            }
            if (s_instance && s_instance->m_originalWndProc) {
                return CallWindowProc((WNDPROC)s_instance->m_originalWndProc, hwnd, msg, wp, lp);
            }
            return DefWindowProc(hwnd, msg, wp, lp);
        }
    );

    return true;
}

// ── System Tray Helpers ─────────────────────────────────────
void UIManager::ShowTrayIcon() {
    m_isMinimized = true;
    glfwHideWindow(m_window);

    NOTIFYICONDATAA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = (HWND)m_hwnd;
    nid.uID = 1001;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Or load custom icon
    strcpy(nid.szTip, "Discord RPC Manager");

    Shell_NotifyIconA(NIM_ADD, &nid);
}

void UIManager::HideTrayIcon() {
    NOTIFYICONDATAA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = (HWND)m_hwnd;
    nid.uID = 1001;
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

void UIManager::RestoreWindow() {
    if (!m_isMinimized) return;
    m_isMinimized = false;
    HideTrayIcon();
    glfwShowWindow(m_window);
    glfwRestoreWindow(m_window);
}

// ── Custom dark theme ───────────────────────────────────────
void UIManager::SetupStyle() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 8.0f;
    s.FrameRounding     = 6.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowPadding     = {16, 16};
    s.FramePadding      = {10, 6};
    s.ItemSpacing       = {10, 8};
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 10.0f;
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.TabBorderSize     = 0.0f;

    auto* c = s.Colors;
    c[ImGuiCol_WindowBg]          = kDarkBg;
    c[ImGuiCol_ChildBg]           = kPanelBg;
    c[ImGuiCol_PopupBg]           = {0.13f, 0.14f, 0.16f, 0.96f};
    c[ImGuiCol_Border]            = {0.22f, 0.23f, 0.27f, 0.50f};
    c[ImGuiCol_FrameBg]           = kInputBg;
    c[ImGuiCol_FrameBgHovered]    = {0.24f, 0.25f, 0.28f, 1.0f};
    c[ImGuiCol_FrameBgActive]     = {0.28f, 0.29f, 0.33f, 1.0f};
    c[ImGuiCol_TitleBg]           = kDarkBg;
    c[ImGuiCol_TitleBgActive]     = kDarkBg;
    c[ImGuiCol_MenuBarBg]         = kDarkBg;
    c[ImGuiCol_Tab]               = {0.18f, 0.19f, 0.22f, 1.0f};
    c[ImGuiCol_TabHovered]        = kBlurple;
    c[ImGuiCol_TabActive]         = kAccentDim;
    c[ImGuiCol_TabSelected]       = kAccentDim;
    c[ImGuiCol_TabSelectedOverline] = kBlurple;
    c[ImGuiCol_Button]            = kBlurple;
    c[ImGuiCol_ButtonHovered]     = {0.40f, 0.45f, 0.98f, 1.0f};
    c[ImGuiCol_ButtonActive]      = {0.30f, 0.34f, 0.85f, 1.0f};
    c[ImGuiCol_Header]            = {0.22f, 0.23f, 0.27f, 1.0f};
    c[ImGuiCol_HeaderHovered]     = {0.28f, 0.30f, 0.35f, 1.0f};
    c[ImGuiCol_HeaderActive]      = kAccentDim;
    c[ImGuiCol_Text]              = {0.95f, 0.96f, 0.98f, 1.0f};
    c[ImGuiCol_TextDisabled]      = kTextDim;
    c[ImGuiCol_CheckMark]         = kBlurple;
    c[ImGuiCol_SliderGrab]        = kBlurple;
    c[ImGuiCol_SliderGrabActive]  = {0.40f, 0.45f, 0.98f, 1.0f};
    c[ImGuiCol_ScrollbarBg]       = {0.12f, 0.13f, 0.15f, 0.50f};
    c[ImGuiCol_ScrollbarGrab]     = {0.30f, 0.31f, 0.35f, 1.0f};
    c[ImGuiCol_SeparatorHovered]  = kBlurple;
    c[ImGuiCol_ResizeGrip]        = {0.26f, 0.59f, 0.98f, 0.20f};
}

// ── File dialog for images ──────────────────────────────────
std::string UIManager::OpenImageFileDialog() {
    char file[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.gif;*.webp;*.bmp\0All\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return std::string(file);
    return "";
}

// ── Shutdown ────────────────────────────────────────────────
void UIManager::Shutdown() {
    HideTrayIcon();
    if (m_hwnd && m_originalWndProc) {
        SetWindowLongPtr((HWND)m_hwnd, GWLP_WNDPROC, (LONG_PTR)m_originalWndProc);
    }
    s_instance = nullptr;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (m_window) glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool UIManager::ShouldClose() const {
    return glfwWindowShouldClose(m_window);
}

bool UIManager::BeginFrame() {
    if (glfwWindowShouldClose(m_window)) return false;
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void UIManager::EndFrame() {
    ImGui::Render();
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(kDarkBg.x, kDarkBg.y, kDarkBg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(m_window);
}

// ── Main Render ─────────────────────────────────────────────
void UIManager::Render(AppState& state) {
    // Sync buffers from state on first frame
    if (!m_buffersSynced) {
        std::lock_guard<std::mutex> lk(state.mtx);
        SyncBuf(m_clientId,     sizeof(m_clientId),     state.rpcConfig.clientId);
        SyncBuf(m_details,      sizeof(m_details),      state.rpcConfig.details);
        SyncBuf(m_state,        sizeof(m_state),        state.rpcConfig.state);
        SyncBuf(m_largeImgKey,  sizeof(m_largeImgKey),  state.rpcConfig.largeImageKey);
        SyncBuf(m_largeImgText, sizeof(m_largeImgText), state.rpcConfig.largeImageText);
        SyncBuf(m_smallImgKey,  sizeof(m_smallImgKey),  state.rpcConfig.smallImageKey);
        SyncBuf(m_smallImgText, sizeof(m_smallImgText), state.rpcConfig.smallImageText);
        SyncBuf(m_btn1Label,    sizeof(m_btn1Label),    state.rpcConfig.buttons[0].label);
        SyncBuf(m_btn1Url,      sizeof(m_btn1Url),      state.rpcConfig.buttons[0].url);
        SyncBuf(m_btn2Label,    sizeof(m_btn2Label),    state.rpcConfig.buttons[1].label);
        SyncBuf(m_btn2Url,      sizeof(m_btn2Url),      state.rpcConfig.buttons[1].url);
        m_useJoinButtons = state.rpcConfig.useJoinButtons;
        m_buffersSynced = true;
    }

    // Fullscreen window
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##Main", nullptr, flags);

    // ── Title bar ──
    ImGui::PushStyleColor(ImGuiCol_Text, kBlurple);
    ImGui::Text(u8"\xF0\x9F\x8E\xB5  Discord RPC Manager");
    ImGui::PopStyleColor();
    ImGui::SameLine(ImGui::GetWindowWidth() - 120);
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        if (state.rpcConnected)
            ImGui::TextColored(kGreen, u8"\xE2\x97\x8F Connected");
        else
            ImGui::TextColored(kRed, u8"\xE2\x97\x8B Disconnected");
    }
    ImGui::Separator();

    // ── Tab bar ──
    if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem(u8"\xF0\x9F\x94\x8C Connection")) {
            RenderConnectionTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"\xE2\x9C\x8F Presence")) {
            RenderPresenceTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"\xF0\x9F\x93\x8B Presets")) {
            RenderPresetsTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"\xF0\x9F\x8E\xB5 Music")) {
            RenderMusicTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"\xF0\x9F\x91\x80 Preview")) {
            RenderPreviewTab(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(u8"\xE2\x9A\x99 Advanced")) {
            RenderAdvancedTab(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    RenderStatusBar(state);
    ImGui::End();
}

// ── Connection Tab ──────────────────────────────────────────
void UIManager::RenderConnectionTab(AppState& state) {
    ImGui::Spacing();

    ImGui::TextColored(kTextDim, "Application Client ID");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##cid", m_clientId, sizeof(m_clientId));

    ImGui::Spacing();

    bool connected;
    { std::lock_guard<std::mutex> lk(state.mtx); connected = state.rpcConnected; }

    if (!connected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kGreen));
        if (ImGui::Button(u8"  \xE2\x9A\xA1 Connect  ", {-1, 36})) {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.rpcConfig.clientId = m_clientId;
            state.needReconnect.store(true);
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kRed));
        if (ImGui::Button(u8"  \xE2\x9C\x96 Disconnect  ", {-1, 36})) {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.rpcConnected = false;
            state.needReconnect.store(false);
            state.needDisconnect.store(true);
        }
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    {
        std::lock_guard<std::mutex> lk(state.mtx);
        if (!state.connectedUsername.empty()) {
            ImGui::TextColored(kGreen, u8"\xE2\x97\x8F Logged in as: %s",
                               state.connectedUsername.c_str());
        }
        if (!state.lastError.empty()) {
            ImGui::TextColored(kRed, u8"\xE2\x9A\xA0 %s",
                               state.lastError.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::TextColored(kTextDim,
        "Create an app at discord.com/developers/applications\n"
        "and paste your Client ID above.");
}

// ── Presence Tab ────────────────────────────────────────────
void UIManager::RenderPresenceTab(AppState& state) {
    ImGui::Spacing();
    bool changed = false;

    ImGui::TextColored(kTextDim, "Details (Line 1)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::InputText("##det", m_details, sizeof(m_details));

    ImGui::TextColored(kTextDim, "State (Line 2)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::InputText("##st", m_state, sizeof(m_state));

    ImGui::Spacing();
    ImGui::SeparatorText("Images (asset key or https:// URL)");

    // Large Image
    ImGui::TextColored(kTextDim, "Large Image");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    changed |= ImGui::InputTextWithHint("##lik", "asset_key or https://...", m_largeImgKey, sizeof(m_largeImgKey));
    ImGui::SameLine();
    if (ImGui::Button("Browse##li", {-1, 0})) {
        std::string path = OpenImageFileDialog();
        if (!path.empty()) {
            SyncBuf(m_largeImgKey, sizeof(m_largeImgKey), path);
            changed = true;
        }
    }
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::InputTextWithHint("##lit", "Hover text", m_largeImgText, sizeof(m_largeImgText));

    // Small Image
    ImGui::TextColored(kTextDim, "Small Image");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80);
    changed |= ImGui::InputTextWithHint("##sik", "asset_key or https://...", m_smallImgKey, sizeof(m_smallImgKey));
    ImGui::SameLine();
    if (ImGui::Button("Browse##si", {-1, 0})) {
        std::string path = OpenImageFileDialog();
        if (!path.empty()) {
            SyncBuf(m_smallImgKey, sizeof(m_smallImgKey), path);
            changed = true;
        }
    }
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::InputTextWithHint("##sit", "Hover text", m_smallImgText, sizeof(m_smallImgText));

    ImGui::Spacing();
    ImGui::SeparatorText("Buttons (clickable links on your profile)");
    if (ImGui::Checkbox("Mode: Use 'Ask to Join' / Invite (requires Discord Game SDK registered client id)", &m_useJoinButtons)) {
        changed = true;
    }
    
    ImGui::Spacing();
    if (!m_useJoinButtons) {
        ImGui::TextColored(kTextDim, "Users can click these buttons to open the URL.");
        ImGui::Spacing();

        // Button 1
        ImGui::Text("Button 1:");
        ImGui::SetNextItemWidth(160);
        changed |= ImGui::InputTextWithHint("##b1l", "Button Name", m_btn1Label, sizeof(m_btn1Label));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputTextWithHint("##b1u", "https://example.com", m_btn1Url, sizeof(m_btn1Url));

        // Button 2
        ImGui::Text("Button 2:");
        ImGui::SetNextItemWidth(160);
        changed |= ImGui::InputTextWithHint("##b2l", "Button Name", m_btn2Label, sizeof(m_btn2Label));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputTextWithHint("##b2u", "https://example.com", m_btn2Url, sizeof(m_btn2Url));
    } else {
        ImGui::TextColored(kTextDim, "Discord will native render 'Ask to Join' when you are connected.");
    }

    ImGui::Spacing();

    // Apply button
    if (ImGui::Button(u8"  \xF0\x9F\x94\x84 Update Presence  ", {-1, 36}) || changed) {
        std::lock_guard<std::mutex> lk(state.mtx);
        state.rpcConfig.details        = m_details;
        state.rpcConfig.state          = m_state;
        state.rpcConfig.largeImageKey  = m_largeImgKey;
        state.rpcConfig.largeImageText = m_largeImgText;
        state.rpcConfig.smallImageKey  = m_smallImgKey;
        state.rpcConfig.smallImageText = m_smallImgText;
        state.rpcConfig.buttons[0]     = {m_btn1Label, m_btn1Url};
        state.rpcConfig.buttons[1]     = {m_btn2Label, m_btn2Url};
        state.rpcConfig.useJoinButtons = m_useJoinButtons;
        state.configDirty.store(true);
    }
}

// ── Presets Tab ─────────────────────────────────────────────
void UIManager::RenderPresetsTab(AppState& state) {
    ImGui::Spacing();

    std::vector<Preset> presets;
    int activeIdx;
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        presets   = state.presets;
        activeIdx = state.activePresetIndex;
    }

    // ── Animate preset switch flash ──
    float dt = ImGui::GetIO().DeltaTime;
    if (m_presetSwitchAnim > 0.0f)
        m_presetSwitchAnim = std::max(0.0f, m_presetSwitchAnim - dt * 3.0f);

    // Flash overlay when switching
    if (m_presetSwitchAnim > 0.0f && activeIdx >= 0 && activeIdx < (int)presets.size()) {
        auto& pc = presets[activeIdx].color;
        ImVec4 flashCol = {pc.r, pc.g, pc.b, m_presetSwitchAnim * 0.15f};
        ImGui::PushStyleColor(ImGuiCol_ChildBg, flashCol);
        ImGui::BeginChild("##flash", {-1, 4}, false);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::TextColored(kTextDim, "Quick switch presets:");
    ImGui::Spacing();

    float btnW = (ImGui::GetContentRegionAvail().x - 30) / 2.0f;
    for (int i = 0; i < (int)presets.size(); ++i) {
        if (i % 2 != 0) ImGui::SameLine();

        bool isActive = (i == activeIdx);
        auto& pc = presets[i].color;

        // ── Per-preset hover animation ──
        float& hoverAnim = m_presetHoverAnim[i < 32 ? i : 31];

        ImVec4 btnCol = {pc.r, pc.g, pc.b, 1.0f};
        ImVec4 btnHov = {std::min(pc.r+0.12f,1.f), std::min(pc.g+0.12f,1.f), std::min(pc.b+0.12f,1.f), 1.0f};
        ImVec4 btnAct = {pc.r*0.8f, pc.g*0.8f, pc.b*0.8f, 1.0f};

        // Active preset gets a brighter shade + subtle pulse
        if (isActive) {
            float pulse = 0.03f * std::sin((float)ImGui::GetTime() * 3.0f);
            btnCol = {std::min(pc.r+0.08f+pulse,1.f), std::min(pc.g+0.08f+pulse,1.f), std::min(pc.b+0.08f+pulse,1.f), 1.0f};
        }

        // Blend hover glow
        float blend = hoverAnim;
        ImVec4 finalCol = {
            btnCol.x + (btnHov.x - btnCol.x) * blend,
            btnCol.y + (btnHov.y - btnCol.y) * blend,
            btnCol.z + (btnHov.z - btnCol.z) * blend,
            1.0f
        };

        ImGui::PushStyleColor(ImGuiCol_Button, finalCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnAct);

        // Active indicator border
        if (isActive) {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1,1,1,0.5f));
        }

        ImGui::PushID(i + 1000);
        std::string label = presets[i].emoji + "  " + presets[i].name;
        if (isActive) label += u8"  \xE2\x9C\x93"; // checkmark

        bool clicked = ImGui::Button(label.c_str(), {btnW, 44});

        // Update hover animation
        bool hovered = ImGui::IsItemHovered();
        float target = hovered ? 1.0f : 0.0f;
        hoverAnim += (target - hoverAnim) * std::min(1.0f, dt * 8.0f);

        if (isActive) {
            ImGui::PopStyleColor(); // Border
            ImGui::PopStyleVar();
        }

        if (clicked) {
            PresetManager::ApplyPreset(presets[i], state);
            {
                std::lock_guard<std::mutex> lk(state.mtx);
                state.activePresetIndex = i;
            }
            m_presetSwitchAnim = 1.0f; // trigger flash
            m_lastActivePreset = i;
            SyncBuf(m_details,      sizeof(m_details),      presets[i].config.details);
            SyncBuf(m_state,        sizeof(m_state),        presets[i].config.state);
            SyncBuf(m_largeImgKey,  sizeof(m_largeImgKey),  presets[i].config.largeImageKey);
            SyncBuf(m_largeImgText, sizeof(m_largeImgText), presets[i].config.largeImageText);
            SyncBuf(m_smallImgKey,  sizeof(m_smallImgKey),  presets[i].config.smallImageKey);
            SyncBuf(m_smallImgText, sizeof(m_smallImgText), presets[i].config.smallImageText);
            SyncBuf(m_btn1Label,    sizeof(m_btn1Label),    presets[i].config.buttons[0].label);
            SyncBuf(m_btn1Url,      sizeof(m_btn1Url),      presets[i].config.buttons[0].url);
            SyncBuf(m_btn2Label,    sizeof(m_btn2Label),    presets[i].config.buttons[1].label);
            SyncBuf(m_btn2Url,      sizeof(m_btn2Url),      presets[i].config.buttons[1].url);
            m_useJoinButtons = presets[i].config.useJoinButtons;
        }

        ImGui::PopID();
        ImGui::PopStyleColor(3);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Show active preset info ──
    if (activeIdx >= 0 && activeIdx < (int)presets.size()) {
        auto& ap = presets[activeIdx];
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(ap.color.r * 0.15f, ap.color.g * 0.15f, ap.color.b * 0.15f, 0.5f));
        ImGui::BeginChild("##activeInfo", {-1, 60}, ImGuiChildFlags_Borders);
        ImGui::TextColored(ImVec4(ap.color.r, ap.color.g, ap.color.b, 1.0f),
            u8"\xE2\x9C\x93 Active: %s %s", ap.emoji.c_str(), ap.name.c_str());
        if (!ap.config.details.empty())
            ImGui::TextColored(kTextDim, "  %s | %s", ap.config.details.c_str(), ap.config.state.c_str());
        if (!ap.config.buttons[0].label.empty())
            ImGui::TextColored(kTextDim, u8"  \xF0\x9F\x94\x97 %s", ap.config.buttons[0].label.c_str());
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    ImGui::TextColored(kTextDim, "Save current config as new preset:");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120);
    ImGui::InputTextWithHint("##pname", "Preset name", m_newPresetName, sizeof(m_newPresetName));
    ImGui::SameLine();
    if (ImGui::Button(u8"\xE2\x9E\x95 Save", {-1, 0})) {
        if (std::strlen(m_newPresetName) > 0) {
            Preset p;
            p.name  = m_newPresetName;
            p.emoji = u8"\xF0\x9F\x93\x8C";
            p.color = {m_newPresetColor[0], m_newPresetColor[1], m_newPresetColor[2]};
            {
                std::lock_guard<std::mutex> lk(state.mtx);
                p.config = state.rpcConfig;
                state.presets.push_back(p);
            }
            PresetManager::SavePresets(m_presetsPath, state.presets);
            m_newPresetName[0] = '\0';
        }
    }
    ImGui::TextColored(kTextDim, "Pick preset button color:");
    ImGui::SetNextItemWidth(200);
    ImGui::ColorEdit3("##presetcol", m_newPresetColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);

    // Delete buttons
    ImGui::Spacing();
    if (presets.size() > 4) {
        ImGui::TextColored(kTextDim, "Delete custom presets:");
        for (int i = 4; i < (int)presets.size(); ++i) {
            ImGui::PushID(i);
            std::string dl = u8"\xE2\x9C\x96 " + presets[i].name;
            if (ImGui::SmallButton(dl.c_str())) {
                std::lock_guard<std::mutex> lk(state.mtx);
                state.presets.erase(state.presets.begin() + i);
                PresetManager::SavePresets(m_presetsPath, state.presets);
            }
            ImGui::PopID();
        }
    }
}

// ── Music Tab ───────────────────────────────────────────────
void UIManager::RenderMusicTab(AppState& state) {
    ImGui::Spacing();

    MediaInfo media;
    LyricData lyrics;
    bool musicMode;
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        media     = state.media;
        lyrics    = state.lyrics;
        musicMode = state.rpcConfig.musicMode;
    }

    // Music mode toggle
    if (ImGui::Checkbox(u8"\xF0\x9F\x8E\xB5 Music Mode (show lyrics on Discord)",
                        &musicMode)) {
        std::lock_guard<std::mutex> lk(state.mtx);
        state.rpcConfig.musicMode = musicMode;
        state.configDirty.store(true);
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Import from YouTube Music");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100);
    ImGui::InputTextWithHint("##yturl", "Paste YouTube Music link here...", m_youtubeUrl, sizeof(m_youtubeUrl));
    ImGui::SameLine();
    if (ImGui::Button("Import", {-1, 0})) {
        if (std::strlen(m_youtubeUrl) > 0) {
            std::lock_guard<std::mutex> lk(state.mtx);
            state.youtubeImportUrl = m_youtubeUrl;
            state.importUrl.store(true);
            state.statusMessage = "Importing from YouTube...";
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Now Playing panel ──
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
    ImGui::BeginChild("##NowPlaying", {-1, 180}, ImGuiChildFlags_Borders);

    if (media.isPlaying && !media.title.empty()) {
        // Title
        ImGui::PushStyleColor(ImGuiCol_Text, kBlurple);
        ImGui::Text(u8"\xF0\x9F\x8E\xB5 Now Playing");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Song info
        ImGui::Text(u8"\xE2\x99\xAA  %s", media.title.c_str());
        ImGui::TextColored(kTextDim, "   %s", media.artist.c_str());
        if (!media.album.empty())
            ImGui::TextColored(kTextDim, "   %s", media.album.c_str());

        ImGui::Spacing();

        // Progress bar
        float progress = (media.durationSeconds > 0)
            ? static_cast<float>(media.positionSeconds / media.durationSeconds)
            : 0.0f;
        progress = std::clamp(progress, 0.0f, 1.0f);

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kBlurple);
        std::string timeStr = Utils::FormatTime(media.positionSeconds)
                            + " / " + Utils::FormatTime(media.durationSeconds);
        ImGui::ProgressBar(progress, {-1, 6}, "");
        ImGui::PopStyleColor();
        ImGui::TextColored(kTextDim, u8"\xE2\x96\xB6 %s   from %s",
                           timeStr.c_str(), media.sourceApp.c_str());

        // Current lyric
        if (lyrics.isSynced && !lyrics.lines.empty()) {
            ImGui::Spacing();
            std::string line = LyricFetcher::GetLineAtPosition(
                lyrics.lines, media.positionSeconds);

            // Animate lyric change
            if (line != m_prevLyricLine) {
                m_prevLyricLine = line;
                m_lastLyricChange = std::chrono::steady_clock::now();
                m_lyricFadeAlpha = 0.0f; // Start fade in
            }

            // Update alpha
            auto now = std::chrono::steady_clock::now();
            float elapsedMs = std::chrono::duration<float, std::milli>(now - m_lastLyricChange).count();
            if (m_lyricFadeAlpha < 1.0f) {
                m_lyricFadeAlpha = std::min(1.0f, elapsedMs / 300.0f); // 300ms fade
            }

            ImVec4 textColor = kBlurple;
            textColor.w = m_lyricFadeAlpha;

            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            ImGui::TextWrapped(u8"\xE2\x99\xAB \"%s\"", line.c_str());
            ImGui::PopStyleColor();
        } else if (lyrics.fetching) {
            ImGui::Spacing();
            ImGui::TextColored(kTextDim, "Fetching lyrics...");
        }
    } else {
        ImGui::Spacing();
        ImGui::TextColored(kTextDim,
            u8"\xF0\x9F\x94\x87 No media playing\n\n"
            "Play something on Spotify or YouTube Music\n"
            "and it will appear here automatically.");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    // ── Lyrics preview ──
    if (!lyrics.lines.empty() && lyrics.isSynced) {
        ImGui::Spacing();
        ImGui::SeparatorText("Synced Lyrics Preview");

        ImGui::BeginChild("##Lyrics", {-1, 200}, ImGuiChildFlags_Borders);
        for (auto& line : lyrics.lines) {
            bool isCurrent = false;
            if (media.isPlaying) {
                size_t idx = &line - &lyrics.lines[0];
                double nextTs = (idx + 1 < lyrics.lines.size())
                    ? lyrics.lines[idx + 1].timestamp : 99999.0;
                isCurrent = media.positionSeconds >= line.timestamp
                         && media.positionSeconds < nextTs;
            }
            if (isCurrent) {
                ImGui::PushStyleColor(ImGuiCol_Text, kBlurple);
                ImGui::Text(u8"\xE2\x96\xB6 %s", line.text.c_str());
                ImGui::PopStyleColor();
                // Auto-scroll to current
                ImGui::SetScrollHereY(0.3f);
            } else {
                ImGui::TextColored(kTextDim, "  %s", line.text.c_str());
            }
        }
        ImGui::EndChild();
    }
}

// ── Status bar ──────────────────────────────────────────────
void UIManager::RenderStatusBar(AppState& state) {
    // Bottom status line
    float footerH = ImGui::GetFrameHeightWithSpacing() + 4;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footerH);
    ImGui::Separator();

    std::lock_guard<std::mutex> lk(state.mtx);
    if (!state.statusMessage.empty()) {
        ImGui::TextColored(kTextDim, "%s", state.statusMessage.c_str());
    } else {
        ImGui::TextColored(kTextDim, "Ready");
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 200);
    ImGui::TextColored(kTextDim, "Rate limit: 15s | v1.0.0");
}

// ── Preview Tab ─────────────────────────────────────────────
void UIManager::RenderPreviewTab(AppState& state) {
    ImGui::Spacing();
    ImGui::TextColored(kTextDim, "Preview of your Rich Presence profile:");
    ImGui::Spacing();

    // Mock Discord Card Background (#2B2D31)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.168f, 0.176f, 0.192f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::BeginChild("##PreviewCard", {-1, 240}, ImGuiChildFlags_Borders);
    
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "PLAYING A GAME");
    ImGui::Spacing();
    
    std::string appName = "Discord RPC Manager"; 
    ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", appName.c_str());
    
    // Media logic or normal logic
    bool musicMode;
    MediaInfo media;
    LyricData lyrics;
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        musicMode = state.rpcConfig.musicMode;
        media = state.media;
        lyrics = state.lyrics;
    }

    std::string det = state.rpcConfig.details;
    std::string st = state.rpcConfig.state;

    if (musicMode && media.isPlaying) {
        det = "♪ ...";
        st = "♪ " + media.artist + " - " + media.title;
    }

    if (!det.empty()) {
        ImGui::TextColored(kTextDim, "%s", det.c_str());
    }
    if (!st.empty()) {
        ImGui::TextColored(kTextDim, "%s", st.c_str());
    }
    
    ImGui::Spacing();
    
    if (state.rpcConfig.useJoinButtons) {
        ImGui::PushStyleColor(ImGuiCol_Button, kGreen);
        ImGui::Button("Ask to Join", {-1, 30});
        ImGui::PopStyleColor();
    } else {
        for (int i=0; i<2; ++i) {
            if (!state.rpcConfig.buttons[i].label.empty()) {
                ImGui::Button(state.rpcConfig.buttons[i].label.c_str(), {-1, 30});
            }
        }
    }
    
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ── Advanced Tab ────────────────────────────────────────────
void UIManager::RenderAdvancedTab(AppState& state) {
    ImGui::Spacing();
    if (ImGui::Button("Clear Logs", {-1, 30})) {
        std::lock_guard<std::mutex> lk(state.mtx);
        state.logMessages.clear();
    }
    ImGui::Spacing();
    
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kPanelBg);
    ImGui::BeginChild("##Logs", {-1, -1}, ImGuiChildFlags_Borders);
    {
        std::lock_guard<std::mutex> lk(state.mtx);
        for (const auto& msg : state.logMessages) {
            ImGui::TextWrapped("%s", msg.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
