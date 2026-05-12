#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <random>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace Utils {

// ── String conversion ────────────────────────────────────────
inline std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                 (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(),
                        out.data(), sz, nullptr, nullptr);
    return out;
}

inline std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(),
                                 nullptr, 0);
    std::wstring out(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(),
                        out.data(), sz);
    return out;
}

// ── Time formatting ──────────────────────────────────────────
inline std::string FormatTime(double seconds) {
    int m = static_cast<int>(seconds) / 60;
    int s = static_cast<int>(seconds) % 60;
    std::ostringstream oss;
    oss << m << ":" << std::setfill('0') << std::setw(2) << s;
    return oss.str();
}

// ── URL encoding ─────────────────────────────────────────────
inline std::string UrlEncode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : value) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << static_cast<char>(c);
        } else {
            escaped << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return escaped.str();
}

// ── Nonce generation ─────────────────────────────────────────
inline std::string GenerateNonce() {
    static std::mt19937_64 gen(std::random_device{}());
    std::uniform_int_distribution<uint64_t> dis;
    std::ostringstream oss;
    oss << std::hex << dis(gen);
    return oss.str();
}

// ── UTF-8 safe truncation ────────────────────────────────────
inline std::string TruncateUTF8(const std::string& str, size_t maxBytes) {
    if (str.size() <= maxBytes) return str;
    size_t len = maxBytes;
    while (len > 0 && (static_cast<unsigned char>(str[len]) & 0xC0) == 0x80)
        --len;
    return str.substr(0, len);
}

// ── Get executable directory ─────────────────────────────────
inline std::string GetExeDirectory() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    auto pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos) : path;
}

} // namespace Utils
