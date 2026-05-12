<div align="center">

# 🎮 Discord RPC Manager

**A powerful, multi-threaded Windows desktop application for fully customizing your Discord Rich Presence — with live music mode, synced lyrics, preset profiles, and a built-in web dashboard.**

[![Platform](https://img.shields.io/badge/Platform-Windows-0078D4?style=for-the-badge&logo=windows)](https://microsoft.com/windows)
[![Language](https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=cplusplus)](https://en.cppreference.com/w/cpp/20)
[![UI](https://img.shields.io/badge/UI-Dear%20ImGui-ff69b4?style=for-the-badge)](https://github.com/ocornut/imgui)
[![Build](https://img.shields.io/badge/Build-CMake%203.21%2B-064F8C?style=for-the-badge&logo=cmake)](https://cmake.org/)
[![License](https://img.shields.io/badge/License-MIT-green?style=for-the-badge)](LICENSE)

[📦 Download](#installation--build-from-source) · [🚀 Quick Start](#quick-start-guide) · [🐛 Report a Bug](../../issues/new)

</div>

---

## 📖 Introduction

**Discord RPC Manager** is a native Windows C++20 application that lets you control exactly what appears on your Discord profile under "Playing a Game". It communicates directly with the Discord Desktop App via the raw **IPC named pipe protocol**, giving you full control over your Rich Presence without any external SDKs.

Beyond a standard RPC editor, the app integrates with the **Windows System Media Transport Controls (SMTC)** API to detect what you're currently playing on Spotify or YouTube Music, automatically fetches **timestamped lyrics** from [lrclib.net](https://lrclib.net), and displays the current and next lyric line in real-time on your Discord profile.

A built-in **REST web server** (port `1337`) also serves a full web dashboard so you can control the application remotely from any browser on your network.

The UI is powered by **Dear ImGui** over **GLFW + OpenGL 3.3**, with full **Vietnamese + Unicode emoji** font support.

![Main Interface](assets/screenshot.png)

---

## ✨ Key Features

- 🎵 **Music Mode** — Automatically detects media playing via Windows SMTC (Spotify, YouTube Music, browsers, etc.) and sets it as your Discord presence.
- 🎤 **Live Synced Lyrics** — Fetches timestamped LRC lyrics from **lrclib.net** and displays the current + next lyric line on your profile, updating in real-time.
- 🖼️ **Custom Images** — Supports both Discord app **asset keys** and direct **`https://`** image URLs for large and small profile images.
- 📋 **Preset Profiles** — Create, save, switch, and color-code multiple Rich Presence configurations. Presets persist as JSON between sessions.
- 👀 **Live Preview Tab** — See a mock-up of your Discord profile card, including buttons and images, *before* you click Connect.
- 🔗 **URL Buttons** — Add up to two clickable URL buttons directly to your Discord profile card.
- 🎮 **Ask to Join / Invite Mode** — Toggle to use Discord's native Party + Secrets IPC payload, enabling the "Ask to Join" and Invite buttons for games.
- 🌐 **Built-in Web Dashboard** — A full REST API and HTML web UI at `http://localhost:1337` to control the app from any browser, with image/GIF/music search.
- 🔍 **Media Search** — Search for images (Pinterest/Unsplash), GIFs (Tenor), and music metadata (Deezer) directly from the web dashboard.
- 📥 **YouTube Music Import** — Paste a YouTube Music URL to instantly import the track metadata and trigger a lyric fetch.
- 🗂️ **System Tray** — Minimize to the Windows system tray and restore with a double-click.
- 📝 **Advanced Log Viewer** — Real-time IPC connection logs (up to 500 lines) viewable in the **Advanced** tab.
- 🌏 **Full Unicode Support** — UI rendered with Arial + Segoe UI Emoji via `IMGUI_USE_WCHAR32`, supporting Vietnamese and full emoji character sets.

---

## 💻 System Requirements

| Requirement | Details |
|---|---|
| **Operating System** | Windows 10 (64-bit) or Windows 11 |
| **Discord** | Discord Desktop App (must be running and logged in) |
| **Compiler** | MSVC 2022 (Visual Studio 17.x) with C++20 support |
| **Build System** | CMake 3.21 or newer |
| **Internet Connection** | Required for lyric fetching, media search, and image loading |
| **Fonts** | `arial.ttf` and `seguiemj.ttf` (included with Windows) |

---

## 🔨 Installation & Build from Source

All dependencies are fetched automatically by CMake via `FetchContent` — no manual installs required.

### 1. Clone the Repository

```bash
git clone https://github.com/YourUsername/DiscordRPCManager.git
cd DiscordRPCManager
```

### 2. Configure with CMake

```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
```

> CMake will automatically download **GLFW**, **Dear ImGui**, and **nlohmann/json** on first configure. This may take a minute.

### 3. Build (Release)

```bash
cmake --build build --config Release
```

The compiled executable will be at:
```
build/Release/DiscordRPCManager.exe
```

### Building with Visual Studio (GUI)

1. Open **Visual Studio 2022**.
2. Select **File → Open → CMake...** and choose the root `CMakeLists.txt`.
3. Visual Studio will automatically configure the project.
4. Select **Release x64** in the toolbar and press **Ctrl+Shift+B** to build.

---

## 🚀 Quick Start Guide

1. **Get a Discord Client ID**
   - Go to [discord.com/developers/applications](https://discord.com/developers/applications) and create a **New Application**.
   - Copy the **Application ID** from the **General Information** page.

2. **Start Discord**
   - Ensure the **Discord Desktop App** is running and you are logged in. The app communicates through Discord's local IPC pipe.

3. **Launch the App**
   - Run `DiscordRPCManager.exe`.

4. **Connect**
   - Go to the **🔌 Connection** tab.
   - Paste your **Application Client ID** into the input field.
   - Click **⚡ Connect**. Your Discord username will appear when successful.

5. **Customize Your Presence**
   - Switch to the **✏️ Presence** tab to set your **Details**, **State**, and images.
   - Click **🔄 Update Presence** to push changes immediately.

6. **Try a Preset**
   - Go to the **📋 Presets** tab and click any preset card to instantly apply a pre-configured status.
   - Use the color picker to customize each preset's accent color.

7. **Enable Music Mode**
   - Go to the **🎵 Music** tab and toggle **Music Mode**.
   - Play something on Spotify or YouTube Music — your presence will update automatically with track info and live lyrics.

8. **Web Dashboard** *(optional)*
   - Open your browser and navigate to `http://localhost:1337` for a full web-based control panel.

---

## 📁 Directory Structure

```
DiscordRPCManager/
│
├── CMakeLists.txt              # Build system — defines targets, deps, flags
├── vcpkg.json                  # vcpkg manifest (optional, deps are via FetchContent)
├── .clangd                     # Clang language server config for IDE support
│
├── assets/
│   ├── web_ui.html             # Bundled HTML for the web dashboard (served at :1337)
│   └── presets_default.json    # Default preset profiles shipped with the app
│
└── src/
    ├── main.cpp                # Entry point — spawns threads, starts web server, runs UI loop
    ├── app_state.h             # Central shared state (thread-safe), all data structs
    │
    ├── discord_manager.cpp/.h  # Discord IPC client — connects, sends SET_ACTIVITY payloads
    ├── ui_manager.cpp/.h       # Dear ImGui UI — all tabs, rendering, system tray
    ├── media_watcher.cpp/.h    # SMTC media watcher — polls currently playing media
    ├── lyric_fetcher.cpp/.h    # Fetches & parses LRC lyrics from lrclib.net
    ├── preset_manager.cpp/.h   # Load/save preset profiles to/from JSON
    ├── web_server.cpp/.h       # Embedded HTTP server (cpp-httplib) + REST API
    │
    ├── utils.h                 # Utility functions (UTF-8, URL encode, time format, nonce)
    └── httplib.h               # Single-header HTTP library (cpp-httplib)
```

---

## 🤝 Contributing

Contributions are very welcome! To get started:

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/my-new-feature`
3. Commit your changes with a clear message: `git commit -m "feat: add my feature"`
4. Push to the branch: `git push origin feature/my-new-feature`
5. Open a **Pull Request** and describe your changes.

> [!NOTE]
> Please ensure the project builds cleanly in both **Debug** and **Release** configurations before submitting a PR.

---

## 📜 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for full details.

---

## ❤️ Credits

This project is built on top of several incredible open-source libraries and free APIs:

| Library / API | Purpose | Link |
|---|---|---|
| **Dear ImGui** | Immediate-mode GUI framework | [ocornut/imgui](https://github.com/ocornut/imgui) |
| **GLFW** | Window creation and OpenGL context | [glfw/glfw](https://github.com/glfw/glfw) |
| **nlohmann/json** | JSON parsing and serialization | [nlohmann/json](https://github.com/nlohmann/json) |
| **cpp-httplib** | Single-header embedded HTTP server | [yhirose/cpp-httplib](https://github.com/yhirose/cpp-httplib) |
| **lrclib.net** | Free, open API for synced LRC lyrics | [lrclib.net](https://lrclib.net) |
| **Deezer API** | Free music metadata and album art search | [developers.deezer.com](https://developers.deezer.com) |
| **Tenor API** | GIF search for image keys | [tenor.com](https://tenor.com/gifapi) |
| **Windows SMTC** | System Media Transport Controls API | [Microsoft Docs](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/system-media-transport-controls) |
| **WinHTTP** | Native Windows HTTPS networking | [Microsoft Docs](https://learn.microsoft.com/en-us/windows/win32/winhttp/winhttp-start-page) |

---

<div align="center">
Made with ❤️ and C++20
</div>
