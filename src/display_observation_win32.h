#pragma once

#include "display_observation.h"
#include <windows.h>
#include <array>

namespace highfps::refresh {

enum class DisplaySource { Unknown, WindowsPath, WindowsMode, WineMode, Gamescope };

struct DisplayReading {
    Observation display{};
    DisplaySource source = DisplaySource::Unknown;
    std::uint32_t id = 0;
};

inline const char* Name(DisplaySource value) {
    switch (value) {
    case DisplaySource::WindowsPath: return "windows-active-path";
    case DisplaySource::WindowsMode: return "windows-window-mode";
    case DisplaySource::WineMode: return "wine-window-mode";
    case DisplaySource::Gamescope: return "gamescope-host";
    case DisplaySource::Unknown: return "unknown";
    }
    return "unknown";
}

inline bool IsWine() {
    static const bool wine = [] {
        const auto library = GetModuleHandleW(L"ntdll.dll");
        return library && GetProcAddress(library, "wine_get_version");
    }();
    return wine;
}

inline HostSnapshot ReadHostSnapshot() {
    wchar_t path[1024] = {};
    const auto length = GetEnvironmentVariableW(L"ISAAC_HIGHFPS_DISPLAY_STATE", path, DWORD(std::size(path)));
    if (!length || length >= std::size(path)) return {};
    const auto file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    char record[160] = {};
    DWORD received = 0;
    const bool read = ReadFile(file, record, sizeof(record) - 1, &received, nullptr) != FALSE;
    CloseHandle(file);
    if (!read || !received || received >= sizeof(record) - 1 || record[received - 1] != '\n') return {};
    for (DWORD index = 0; index < received; ++index)
        if (!record[index]) return {};
    FILETIME wall{};
    GetSystemTimeAsFileTime(&wall);
    constexpr std::uint64_t unixEpoch = 116444736000000000ull;
    const auto ticks = (std::uint64_t(wall.dwHighDateTime) << 32) | wall.dwLowDateTime;
    return ticks >= unixEpoch ? ParseHostSnapshot(record, (ticks - unixEpoch) / 10000) : HostSnapshot{};
}

inline bool SameAdapter(LUID left, LUID right) {
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

inline Rate SignalRate(DISPLAYCONFIG_RATIONAL signal) {
    const Rate rate{signal.Numerator, signal.Denominator};
    return rate.Hz() > 1 ? rate : Rate{};
}

inline Rate ActivePathRate(const wchar_t* device) {
    const auto user = GetModuleHandleW(L"user32.dll");
    if (!user) return {};
    using GetSizes = LONG(WINAPI*)(UINT32, UINT32*, UINT32*);
    using GetPaths = LONG(WINAPI*)(UINT32, UINT32*, DISPLAYCONFIG_PATH_INFO*, UINT32*,
                                   DISPLAYCONFIG_MODE_INFO*, DISPLAYCONFIG_TOPOLOGY_ID*);
    using GetInfo = LONG(WINAPI*)(DISPLAYCONFIG_DEVICE_INFO_HEADER*);
    const auto size = reinterpret_cast<GetSizes>(GetProcAddress(user, "GetDisplayConfigBufferSizes"));
    const auto query = reinterpret_cast<GetPaths>(GetProcAddress(user, "QueryDisplayConfig"));
    const auto info = reinterpret_cast<GetInfo>(GetProcAddress(user, "DisplayConfigGetDeviceInfo"));
    if (!size || !query || !info) return {};
    std::array<DISPLAYCONFIG_PATH_INFO, 64> paths{};
    std::array<DISPLAYCONFIG_MODE_INFO, 192> modes{};
    constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        UINT32 pathCount = 0, modeCount = 0;
        if (size(flags, &pathCount, &modeCount) != ERROR_SUCCESS ||
            pathCount > paths.size() || modeCount > modes.size()) return {};
        const LONG queried = query(flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (queried == ERROR_INSUFFICIENT_BUFFER) continue;
        if (queried != ERROR_SUCCESS || pathCount > paths.size() || modeCount > modes.size()) return {};
        const DISPLAYCONFIG_PATH_INFO* selected = nullptr;
        for (UINT32 index = 0; index < pathCount; ++index) {
            const auto& path = paths[index];
            if (!(path.flags & DISPLAYCONFIG_PATH_ACTIVE) || !path.targetInfo.targetAvailable) continue;
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            name.header.size = sizeof(name);
            name.header.adapterId = path.sourceInfo.adapterId;
            name.header.id = path.sourceInfo.id;
            if (info(&name.header) != ERROR_SUCCESS || lstrcmpiW(device, name.viewGdiDeviceName) != 0) continue;
            if (selected) return {};
            selected = &path;
        }
        if (!selected) return {};
        const auto& target = selected->targetInfo;
        if (target.modeInfoIdx < modeCount) {
            const auto& mode = modes[target.modeInfoIdx];
            if (mode.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET && mode.id == target.id &&
                SameAdapter(mode.adapterId, target.adapterId)) {
                const Rate rate = SignalRate(mode.targetMode.targetVideoSignalInfo.vSyncFreq);
                if (rate.Valid()) return rate;
            }
        }
        return SignalRate(target.refreshRate);
    }
    return {};
}

inline HWND OutputWindow(HDC drawable) {
    static const auto windowFromDc = [] {
        const auto user = GetModuleHandleW(L"user32.dll");
        return user ? reinterpret_cast<decltype(&WindowFromDC)>(GetProcAddress(user, "WindowFromDC")) : nullptr;
    }();
    return windowFromDc ? windowFromDc(drawable) : nullptr;
}

inline DisplayReading ReadDisplay(HWND window) {
    const bool wine = IsWine();
    if (wine) {
        const auto host = ReadHostSnapshot();
        if (host.valid) return {host.display, DisplaySource::Gamescope, host.outputId};
    }
    const auto user = GetModuleHandleW(L"user32.dll");
    if (!window || !user) return {};
    const auto monitorFromWindow =
        reinterpret_cast<decltype(&MonitorFromWindow)>(GetProcAddress(user, "MonitorFromWindow"));
    const auto getMonitorInfo =
        reinterpret_cast<decltype(&GetMonitorInfoW)>(GetProcAddress(user, "GetMonitorInfoW"));
    const auto getMode =
        reinterpret_cast<decltype(&EnumDisplaySettingsExW)>(GetProcAddress(user, "EnumDisplaySettingsExW"));
    if (!monitorFromWindow || !getMonitorInfo) return {};
    const auto monitor = monitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW properties{};
    properties.cbSize = sizeof(properties);
    if (!monitor || !getMonitorInfo(monitor, &properties)) return {};
    const auto id = std::uint32_t(reinterpret_cast<std::uintptr_t>(monitor));
    if (!wine) {
        const auto precise = ActivePathRate(properties.szDevice);
        if (precise.Valid()) return {{precise}, DisplaySource::WindowsPath, id};
    }
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    if (getMode && getMode(properties.szDevice, ENUM_CURRENT_SETTINGS, &current, 0) &&
        (current.dmFields & DM_DISPLAYFREQUENCY) && current.dmDisplayFrequency > 1 &&
        current.dmDisplayFrequency <= 1000)
        return {{{current.dmDisplayFrequency, 1}}, wine ? DisplaySource::WineMode : DisplaySource::WindowsMode, id};
    return {};
}

}
